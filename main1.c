#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_CHAIRS 40         // Liczba krzesełek
#define MAX_PEOPLE_ON_CHAIR 3 // Liczba miejsc na jednym krzesełku
#define MAX_PEOPLE_ON_PLATFORM 50 // Maksymalna liczba osób na peronie
#define NUM_WORKERS 2         // Liczba pracowników
#define OPENING_HOUR 8
#define CLOSING_HOUR 10
#define SIMULATION_STEP 1 // 1 sekunda = 1 minuta w symulacji

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

// Struktura komunikatu dla kolejki komunikatów
typedef struct {
    long message_type;  // Typ komunikatu
    int skier_id;       // ID narciarza
    int age;            // Wiek narciarza
    bool is_vip;        // Czy narciarz jest VIP
} GateMessage;

// Kolejka komunikatów
key_t gate_key;
int gate_msg_queue;

// Inicjalizacja kolejki komunikatów
void init_message_queue() {
    gate_key = ftok("ski_station", 65);
    gate_msg_queue = msgget(gate_key, 0666 | IPC_CREAT);
    if (gate_msg_queue == -1) {
        perror("Nie udało się utworzyć kolejki komunikatów");
        exit(EXIT_FAILURE);
    }
}

// Usuwanie kolejki komunikatów
void cleanup_message_queue() {
    if (msgctl(gate_msg_queue, IPC_RMID, NULL) == -1) {
        perror("Nie udało się usunąć kolejki komunikatów");
    }
}

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
        pthread_cond_broadcast(&lift_condition);
    }
    pthread_mutex_unlock(&lift_mutex);
}

// Funkcja wątku pracownika
void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (is_station_open) {
        if (rand() % 10 == 0) { // 10% szans na zatrzymanie kolejki
            stop_lift(worker_id);
            sleep(2); // Symulacja problemu
            resume_lift(worker_id);
        }
        sleep(1); // Czas pracy pracownika
    }

    printf("[Pracownik #%d] Kończy pracę.\n", worker_id);
    return NULL;
}

// Wątek narciarza
void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;

    while (simulated_time < skier->ticket->expiry_time && is_station_open) {
        pthread_mutex_lock(&lift_mutex);
        while (!is_lift_running) {
            pthread_cond_wait(&lift_condition, &lift_mutex);
        }
        pthread_mutex_unlock(&lift_mutex);

        sem_wait(&chairlift_sem);
        printf("Narciarz #%d wsiada na krzesełko.\n", skier->skier_id);
        sleep(2);
        sem_post(&chairlift_sem);

        printf("Narciarz #%d zjeżdża trasą.\n", skier->skier_id);
        sleep(rand() % 3 + 1);

        skier->ticket->usage_count++;
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
    return NULL;
}

int main() {
    srand(time(NULL));
    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS * MAX_PEOPLE_ON_CHAIR);

    pthread_t time_thread, worker_threads[NUM_WORKERS];
    pthread_t skier_threads[MAX_PEOPLE_ON_PLATFORM];

    init_message_queue();

    pthread_create(&time_thread, NULL, time_simulation_thread, NULL);

    // Tworzenie wątków pracowników
    for (int i = 0; i < NUM_WORKERS; i++) {
        int* worker_id = malloc(sizeof(int));
        *worker_id = i + 1;
        pthread_create(&worker_threads[i], NULL, worker_thread, worker_id);
    }

    // Tworzenie wątków narciarzy
    for (int i = 0; i < MAX_PEOPLE_ON_PLATFORM; i++) {
        Skier* skier = malloc(sizeof(Skier));
        skier->skier_id = i + 1;
        skier->age = rand() % 75 + 4;
        skier->ticket = purchase_ticket(skier->skier_id, skier->age);
        skier->is_guardian = skier->age >= 18 && skier->age <= 65;
        skier->is_child = false;

        pthread_create(&skier_threads[i], NULL, skier_thread, skier);
        usleep(rand() % 500000);
    }

    for (int i = 0; i < MAX_PEOPLE_ON_PLATFORM; i++) {
        pthread_join(skier_threads[i], NULL);
    }

    pthread_join(time_thread, NULL);
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(worker_threads[i], NULL);
    }

    cleanup_message_queue();
    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);

    printf("Program zakończył działanie.\n");
    return 0;
}
