 #include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define MAX_CHAIRS 40         // Liczba krzesełek
#define MAX_PEOPLE_ON_CHAIR 3 // Liczba miejsc na jednym krzesełku
#define MAX_PEOPLE_ON_PLATFORM 50 // Maksymalna liczba osób na peronie
#define NUM_WORKERS 2         // Liczba pracowników
#define OPENING_HOUR 8
#define CLOSING_HOUR 10
#define SIMULATION_STEP 1 // 1 sekunda = 1 minuta w symulacji

// Dodanie tras
#define T1_TIME 2  // Czas przejazdu trasy T1 (w sekundach)
#define T2_TIME 4  // Czas przejazdu trasy T2 (w sekundach)
#define T3_TIME 6  // Czas przejazdu trasy T3 (w sekundach)

// Symulowany czas
volatile int simulated_time = 0; 
volatile bool is_station_open = true;
volatile bool is_lift_running = true;
volatile int people_on_lift = 0;

// Semafory
sem_t platform_sem;
sem_t chairlift_sem;

// Mutexy i zmienne warunkowe
pthread_mutex_t lift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t station_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t lift_condition = PTHREAD_COND_INITIALIZER;

// Struktura karnetu (biletu)
typedef struct {
    int ticket_id;      // ID karnetu
    int usage_count;    // Liczba wykorzystanych przejazdów
    bool is_vip;        // Czy bilet jest VIP?
    int expiry_time;    // Czas ważności karnetu w minutach
} Ticket;

// Struktura narciarza
typedef struct {
    int skier_id;       // ID narciarza
    int age;            // Wiek
    bool is_vip;        // Czy VIP
    bool is_guardian;   // Czy opiekun
    int children_count; // Liczba dzieci pod opieką
    int guardian_id;    // ID opiekuna (dla dzieci)
    Ticket* ticket;     // Wskaźnik na bilet
    bool is_child;      // Czy narciarz jest dzieckiem
} Skier;

// Wskaźnik do pamięci dzielonej
int* shared_usage;

// Zakup biletu
Ticket* purchase_ticket(int skier_id, int age) {
    Ticket* ticket = malloc(sizeof(Ticket));
    ticket->ticket_id = skier_id;
    ticket->usage_count = 0;

    int ticket_type = rand() % 4;
    if (ticket_type == 3) {
        ticket->expiry_time = (CLOSING_HOUR - OPENING_HOUR) * 60; // Bilet dzienny
    } else {
        ticket->expiry_time = (ticket_type + 1) * 60; // Tk1, Tk2, Tk3
    }

    ticket->is_vip = rand() % 5 == 0; // 20% szans na VIP

    if (age < 12 || age > 65) {
        printf("Narciarz #%d otrzymał zniżkę na karnet.\n", skier_id);
    }

    return ticket;
}

// Funkcja zatrzymująca kolejkę linową
void stop_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (is_lift_running) {
        is_lift_running = false;
        printf("Kolejka linowa została zatrzymana przez pracownika #%d.\n", worker_id);
    }
    pthread_mutex_unlock(&lift_mutex);
}

// Funkcja wznawiająca kolejkę linową
void resume_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (!is_lift_running) {
        is_lift_running = true;
        printf("Kolejka linowa została wznowiona przez pracownika #%d.\n", worker_id);
        pthread_cond_broadcast(&lift_condition); // Powiadom wszystkich narciarzy
    }
    pthread_mutex_unlock(&lift_mutex);
}

// Funkcja obsługi dzieci i opiekunów
bool can_ski(Skier* skier) {
    if (skier->is_child) {
        if (skier->guardian_id == -1) {
            printf("Dziecko #%d nie ma opiekuna i nie może korzystać z kolejki.\n", skier->skier_id);
            return false;
        }
    }
    return true;
}

// Funkcja wątku narciarza
void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;

    if (!can_ski(skier)) {
        free(skier->ticket);
        free(skier);
        return NULL;
    }

    while (simulated_time < skier->ticket->expiry_time && is_station_open) {
        pthread_mutex_lock(&lift_mutex);
        while (!is_lift_running) { // Czekaj na wznowienie kolejki
            printf("Narciarz #%d czeka na wznowienie kolejki.\n", skier->skier_id);
            pthread_cond_wait(&lift_condition, &lift_mutex); // Czekaj na sygnał od pracownika
        }
        pthread_mutex_unlock(&lift_mutex);

        if (skier->ticket->is_vip) {
            sem_wait(&chairlift_sem);
            printf("[VIP] Narciarz #%d wsiada na krzesełko.\n", skier->skier_id);
        } else {
            sem_wait(&platform_sem);
            sem_wait(&chairlift_sem);
            printf("Narciarz #%d wsiada na krzesełko.\n", skier->skier_id);
        }

        sleep(rand() % 3 + 1);  // Czas jazdy
        skier->ticket->usage_count++;

        // Zapis zjazdu w pamięci dzielonej
        shared_usage[skier->skier_id]++;

        sem_post(&chairlift_sem);
        sem_post(&platform_sem);

        int track_choice = rand() % 3;  // Wybór trasy
        if (track_choice == 0) {
            sleep(T1_TIME);
            printf("Narciarz #%d zjeżdża trasą T1.\n", skier->skier_id);
        } else if (track_choice == 1) {
            sleep(T2_TIME);
            printf("Narciarz #%d zjeżdża trasą T2.\n", skier->skier_id);
        } else {
            sleep(T3_TIME);
            printf("Narciarz #%d zjeżdża trasą T3.\n", skier->skier_id);
        }
    }

    printf("Narciarz #%d kończy dzień na stacji.\n", skier->skier_id);
    free(skier->ticket);
    free(skier);
    return NULL;
}

// Symulacja czasu
void* time_simulation_thread(void* arg) {
    int simulation_duration = (CLOSING_HOUR - OPENING_HOUR) * 60;

    while (simulated_time < simulation_duration) {
        sleep(SIMULATION_STEP);
        simulated_time += 2; // 1 sekunda = 2 minuty
        if (simulated_time % 60 == 0) {
            printf("Symulowany czas: %d godzin minęło.\n", simulated_time / 60);
        }
    }

    pthread_mutex_lock(&station_mutex);
    is_station_open = false;
    pthread_mutex_unlock(&station_mutex);

    printf("Stacja zamyka się.\n");
    sleep(5);  // Czas na wyłączenie kolejki

    return NULL;
}
void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (is_station_open) {
        if (rand() % 10 == 0) { // 10% szans na zatrzymanie kolejki
            stop_lift(worker_id);
            sleep(2); // Symulacja problemu, kolejka zatrzymana na 2 sekundy
            resume_lift(worker_id);
        }
        sleep(1); // Czas pracy pracownika
    }

    printf("[Pracownik #%d] Kończy pracę.\n", worker_id);
    return NULL;
}


int main() {
    srand(time(NULL));
    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS * MAX_PEOPLE_ON_CHAIR);

    // Inicjalizacja pamięci dzielonej
    int shm_id = shmget(IPC_PRIVATE, sizeof(int) * MAX_PEOPLE_ON_PLATFORM, IPC_CREAT | 0666);
    shared_usage = shmat(shm_id, NULL, 0);
    for (int i = 0; i < MAX_PEOPLE_ON_PLATFORM; i++) {
        shared_usage[i] = 0;
    }

    pthread_t time_thread, skier_threads[MAX_PEOPLE_ON_PLATFORM];

    pthread_create(&time_thread, NULL, time_simulation_thread, NULL);

    // Tworzenie wątków narciarzy
    for (int i = 0; i < MAX_PEOPLE_ON_PLATFORM; i++) {
        Skier* skier = malloc(sizeof(Skier));
        skier->skier_id = i;
        skier->age = rand() % 75 + 4;
        skier->ticket = purchase_ticket(skier->skier_id, skier->age);
        skier->is_guardian = skier->age >= 18 && skier->age <= 65;
        skier->is_child = skier->age >= 4 && skier->age <= 8;
        skier->guardian_id = skier->is_child ? (rand() % (i + 1)) : -1;

        pthread_create(&skier_threads[i], NULL, skier_thread, skier);
        usleep(rand() % 500000);
    }

    for (int i = 0; i < MAX_PEOPLE_ON_PLATFORM; i++) {
        pthread_join(skier_threads[i], NULL);
    }

    pthread_join(time_thread, NULL);

    // Wyświetlenie raportu po zakończeniu wszystkich wątków
    printf("\n[Raport dzienny z pamięci dzielonej]\n");
    for (int i = 0; i < MAX_PEOPLE_ON_PLATFORM; i++) {
        if (shared_usage[i] > 0) {
            printf("Narciarz #%d wykonał %d zjazdów.\n", i, shared_usage[i]);
        }
    }

    // Zwolnienie pamięci dzielonej
    shmdt(shared_usage);
    shmctl(shm_id, IPC_RMID, NULL);

    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);

    printf("Program zakończył działanie.\n");
    return 0;
}
