#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

#define MAX_CHAIRS 40         // Liczba krzesełek
#define MAX_PEOPLE_ON_CHAIR 3 // Liczba miejsc na jednym krzesełku
#define MAX_PEOPLE_ON_PLATFORM 50 // Maksymalna liczba osób na peronie
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
sem_t lift_control_sem;

// Mutexy i zmienne warunkowe
pthread_mutex_t lift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t station_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t lift_condition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t chairlift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lift_operation_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex do synchronizacji operacji

// Struktura komunikatu
typedef struct {
    long message_type; // Typ komunikatu
    char message_text[100]; // Treść komunikatu
} Message;

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

// ID kolejki komunikatów
int msgid;

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
        pthread_cond_broadcast(&lift_condition); // Powiadomienie wszystkich o zatrzymaniu
    }
    pthread_mutex_unlock(&lift_mutex);
}

void resume_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (!is_lift_running) {
        is_lift_running = true;
        printf("Kolejka linowa została wznowiona przez pracownika #%d.\n", worker_id);
        pthread_cond_broadcast(&lift_condition); // Powiadomienie wszystkich o wznowieniu
    }
    pthread_mutex_unlock(&lift_mutex);
}

// Symulacja czasu
void* time_simulation_thread(void* arg) {
    int simulation_duration = (CLOSING_HOUR - OPENING_HOUR) * 60;

    while (simulated_time < simulation_duration) {
        sleep(SIMULATION_STEP);
        simulated_time += 2; // 1 sekunda = 2 minuty
        if (simulated_time % 60 == 0) {
            printf("Symulowany czas: %d godzin min   ^y   ^bo.\n", simulated_time / 60);
        }
    }

    pthread_mutex_lock(&station_mutex);
    is_station_open = false;
    pthread_mutex_unlock(&station_mutex);

    printf("Stacja zamyka si   ^y.\n");
    sleep(5);  // Czas na wy   ^b   ^eczenie kolejki

    return NULL;
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
// Funkcja wątku narciarza
void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;

    if (!can_ski(skier)) {
        free(skier->ticket);
        free(skier);
        return NULL;
    }

    while (simulated_time < skier->ticket->expiry_time && is_station_open) {
        // Oczekiwanie na miejsce na platformie
        sem_wait(&platform_sem);
        printf("Narciarz #%d wchodzi na platformę.\n", skier->skier_id);

        pthread_mutex_lock(&lift_mutex);
        while (!is_lift_running) { // Czekaj na wznowienie kolejki
            printf("Narciarz #%d czeka na wznowienie kolejki.\n", skier->skier_id);
            pthread_cond_wait(&lift_condition, &lift_mutex);
        }
        pthread_mutex_unlock(&lift_mutex);

// Narciarz wsiada na krzesełko
sem_wait(&chairlift_sem);
pthread_mutex_lock(&lift_operation_mutex);
while (!is_lift_running) {
    printf("Narciarz #%d czeka na wznowienie kolejki przed wsiadaniem.\n", skier->skier_id);
    pthread_cond_wait(&lift_condition, &lift_operation_mutex);
}
printf("Narciarz #%d wsiada na krzesełko.\n", skier->skier_id);
pthread_mutex_unlock(&lift_operation_mutex);

// Symulacja jazdy
int ride_time = rand() % 3 + 1; // Czas jazdy w sekundach
for (int t = 0; t < ride_time; t++) {
    pthread_mutex_lock(&lift_operation_mutex);
    while (!is_lift_running) {
        printf("Narciarz #%d zatrzymuje się na krzesełku i czeka na wznowienie.\n", skier->skier_id);
        pthread_cond_wait(&lift_condition, &lift_operation_mutex);
    }
    pthread_mutex_unlock(&lift_operation_mutex);
    sleep(1); // Symulacja jednej sekundy jazdy
}

// Narciarz kończy jazdę
pthread_mutex_lock(&lift_operation_mutex);
while (!is_lift_running) {
    printf("Narciarz #%d czeka na wznowienie kolejki przed zejściem.\n", skier->skier_id);
    pthread_cond_wait(&lift_condition, &lift_operation_mutex);
}
printf("Narciarz #%d kończy jazdę krzesełkiem i schodzi z platformy.\n", skier->skier_id);
pthread_mutex_unlock(&lift_operation_mutex);

skier->ticket->usage_count++;

// Zapis zjazdu w pamięci dzielonej
shared_usage[skier->skier_id]++;
sem_post(&chairlift_sem); // Zwolnienie miejsca na krzesełku
sem_post(&platform_sem);  // Zwolnienie miejsca na platformie

        // Wybór trasy i czas przejazdu
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

void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (is_station_open) {
        if (rand() % 10 == 0) { // 10% szans na zatrzymanie kolejki
            Message msg;
            msg.message_type = 1; // Prośba o gotowość
            snprintf(msg.message_text, sizeof(msg.message_text), "Pracownik #%d zatrzymuje kolejkę.", worker_id);

            stop_lift(worker_id);

            printf("Pracownik #%d wysyła zapytanie do pracownika #%d o gotowość.\n", worker_id, 2);
            msgsnd(msgid, &msg, sizeof(msg), 0);

            msgrcv(msgid, &msg, sizeof(msg), 2, 0); // Czekaj na odpowiedź od Pracownika 2
            printf("Pracownik #%d otrzymał odpowiedź: %s\n", worker_id, msg.message_text);

            resume_lift(worker_id);
        }
        sleep(1); // Czas pracy pracownika
    }

    printf("[Pracownik #%d] Kończy pracę.\n", worker_id);
    return NULL;
}

void* responder_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (is_station_open) {
        Message msg;
        msgrcv(msgid, &msg, sizeof(msg), 1, 0);

        // Sprawdź, czy to sygnał zakończenia
        if (strcmp(msg.message_text, "END") == 0) {
            printf("[Pracownik #%d] Kończy pracę na podstawie sygnału zakończenia.\n", worker_id);
            break;
        }

        printf("Pracownik #%d otrzymał komunikat: %s\n", worker_id, msg.message_text);
        sleep(2); // Symulacja sprawdzania gotowości

        msg.message_type = 2; // Odpowiedź do Pracownika 1
        snprintf(msg.message_text, sizeof(msg.message_text), "Pracownik #%d gotowy do wznowienia.", worker_id);
        msgsnd(msgid, &msg, sizeof(msg), 0);
    }

    return NULL;
}

int main() {
    srand(time(NULL));
    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS * MAX_PEOPLE_ON_CHAIR);
    sem_init(&lift_control_sem, 0, 1); // Semafor początkowo odblokowany

    // Inicjalizacja pamięci dzielonej
    int shm_id = shmget(IPC_PRIVATE, sizeof(int) * 1000, IPC_CREAT | 0666); // Zakładamy maksymalnie 1000 narciarzy
    shared_usage = shmat(shm_id, NULL, 0);
    for (int i = 0; i < 1000; i++) {
        shared_usage[i] = 0;
    }

    // Inicjalizacja kolejki komunikatów
    msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);

    pthread_t time_thread;
    pthread_create(&time_thread, NULL, time_simulation_thread, NULL);

    // Tworzenie wątków pracowników
    pthread_t worker_thread_id, responder_thread_id;
    int* worker_id = malloc(sizeof(int));
    *worker_id = 1;
    pthread_create(&worker_thread_id, NULL, worker_thread, worker_id);

    int* responder_id = malloc(sizeof(int));
    *responder_id = 2;
    pthread_create(&responder_thread_id, NULL, responder_thread, responder_id);

    // Tworzenie wątków narciarzy w nieskończonej pętli
    int skier_id = 0;
    while (is_station_open) {
        Skier* skier = malloc(sizeof(Skier));
        skier->skier_id = skier_id;
        skier->age = rand() % 75 + 4;
        skier->ticket = purchase_ticket(skier->skier_id, skier->age);
        skier->is_guardian = skier->age >= 18 && skier->age <= 65;
        skier->is_child = skier->age >= 4 && skier->age <= 8;
        skier->guardian_id = skier->is_child ? (rand() % (skier_id + 1)) : -1;

        pthread_t skier_thread_id;
        pthread_create(&skier_thread_id, NULL, skier_thread, skier);
        usleep(rand() % 500000); // Nowy narciarz co 0.5 sekundy

        skier_id++;
    }

    pthread_join(time_thread, NULL);

    // Wysłanie sygnału zakończenia do responder_thread
    Message end_msg;
    end_msg.message_type = 1;
    snprintf(end_msg.message_text, sizeof(end_msg.message_text), "END");
    msgsnd(msgid, &end_msg, sizeof(end_msg), 0);
    pthread_join(worker_thread_id, NULL);
    pthread_join(responder_thread_id, NULL);

	

    // Wyświetlenie raportu po zakończeniu wszystkich wątków
	sleep(5);
    printf("\n[Raport dzienny z pamięci dzielonej]\n");
    for (int i = 0; i < skier_id; i++) {
        if (shared_usage[i] > 0) {
            printf("Narciarz #%d wykonał %d zjazdów.\n", i, shared_usage[i]);
        }
    }

    // Zwolnienie pamięci dzielonej
    shmdt(shared_usage);
    shmctl(shm_id, IPC_RMID, NULL);

    // Usunięcie kolejki komunikatów
    msgctl(msgid, IPC_RMID, NULL);

    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);

    printf("Program zakończył działanie.\n");
    return 0;
}
