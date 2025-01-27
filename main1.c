#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define MAX_CHAIRS 40
#define MAX_SKIER_COUNT 120
#define MAX_PEOPLE_ON_PLATFORM 50
#define OPENING_HOUR 8
#define CLOSING_HOUR 18

typedef struct {
    int skier_id;
    int age;
    bool is_vip;
    int usage_count;
} Skier;

sem_t platform_sem;
sem_t chairlift_sem;
pthread_mutex_t station_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t station_close_cond = PTHREAD_COND_INITIALIZER;

volatile bool is_station_open = true;
volatile bool is_lift_running = true; // Flaga określająca działanie kolejki
volatile bool is_being_reset = false; // Czy kolejka jest w trakcie resetu?
volatile int active_skiers = 0;

pthread_mutex_t lift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t lift_condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t reset_condition = PTHREAD_COND_INITIALIZER;

void perror_exit(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// Funkcja zatrzymująca kolejkę
void stop_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);

    // Jeśli kolejka jest już zatrzymywana, nie rób nic
    if (is_being_reset || !is_lift_running) {
        pthread_mutex_unlock(&lift_mutex);
        return;
    }

    is_lift_running = false;
    is_being_reset = true;
    printf("Kolejka linowa została zatrzymana przez pracownika #%d.\n", worker_id);

    pthread_mutex_unlock(&lift_mutex);
}

// Funkcja wznawiająca kolejkę
void resume_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);

    // Wznowienie tylko jeśli kolejka była wcześniej zatrzymana
    if (is_being_reset) {
        is_lift_running = true;
        is_being_reset = false;
        printf("Kolejka linowa została wznowiona przez pracownika #%d.\n", worker_id);
        pthread_cond_broadcast(&lift_condition); // Wznowienie narciarzy
        pthread_cond_signal(&reset_condition);   // Powiadomienie innych pracowników
    }

    pthread_mutex_unlock(&lift_mutex);
}

void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;

    pthread_mutex_lock(&station_mutex);
    active_skiers++;
    pthread_mutex_unlock(&station_mutex);

    printf("Narciarz #%d wchodzi na peron.\n", skier->skier_id);

    sem_wait(&platform_sem); // Wejście na platformę
    printf("Narciarz #%d jest na platformie.\n", skier->skier_id);

    // Czekanie na wznowienie kolejki
    pthread_mutex_lock(&lift_mutex);
    while (!is_lift_running) {
        printf("Narciarz #%d czeka na wznowienie kolejki.\n", skier->skier_id);
        pthread_cond_wait(&lift_condition, &lift_mutex);
    }
    pthread_mutex_unlock(&lift_mutex);

    sem_wait(&chairlift_sem); // Wejście na krzesełko
    printf("Narciarz #%d wsiada na krzesełko.\n", skier->skier_id);
    sleep(2); // Jazda krzesełkiem
    sem_post(&chairlift_sem);

    printf("Narciarz #%d zjeżdża trasą.\n", skier->skier_id);
    sleep(3); // Zjazd trasą
    sem_post(&platform_sem);

    printf("Narciarz #%d kończy dzień na stacji.\n", skier->skier_id);

    pthread_mutex_lock(&station_mutex);
    active_skiers--;
    pthread_cond_signal(&station_close_cond);
    pthread_mutex_unlock(&station_mutex);

    free(skier);
    return NULL;
}

void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (is_station_open || active_skiers > 0) {
        pthread_mutex_lock(&lift_mutex);

        if (rand() % 10 == 0 && is_lift_running && !is_being_reset) { // Symulacja zatrzymania
            pthread_mutex_unlock(&lift_mutex);
            stop_lift(worker_id);
            sleep(2); // Symulacja analizy sytuacji
            printf("[Pracownik #%d] Komunikuje się z drugim pracownikiem...\n", worker_id);
            sleep(1); // Komunikacja
            resume_lift(worker_id);
        } else {
            pthread_mutex_unlock(&lift_mutex);
        }

        sleep(1); // Pracownik obsługuje stację
    }

    printf("[Pracownik #%d] Kończy pracę.\n", worker_id);
    return NULL;
}

void close_station() {
    printf("Zamykanie stacji...\n");

    pthread_mutex_lock(&station_mutex);
    is_station_open = false;
    while (active_skiers > 0) {
        pthread_cond_wait(&station_close_cond, &station_mutex);
    }
    pthread_mutex_unlock(&station_mutex);

    printf("Wszyscy narciarze opuścili stację. Stacja zamknięta.\n");
}

int main() {
    srand(time(NULL));

    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS);

    pthread_t skier_threads[MAX_SKIER_COUNT];
    pthread_t worker1, worker2;

    int* worker1_id = malloc(sizeof(int));
    int* worker2_id = malloc(sizeof(int));
    *worker1_id = 1;
    *worker2_id = 2;

    // Tworzenie wątków pracowników
    if (pthread_create(&worker1, NULL, worker_thread, worker1_id) != 0) {
        perror_exit("Nie udało się utworzyć wątku dla pracownika 1");
    }

    if (pthread_create(&worker2, NULL, worker_thread, worker2_id) != 0) {
        perror_exit("Nie udało się utworzyć wątku dla pracownika 2");
    }

    // Tworzenie wątków narciarzy
    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        Skier* skier = malloc(sizeof(Skier));
        skier->skier_id = i + 1;
        skier->age = rand() % 60 + 10; // Losowy wiek narciarza
        skier->is_vip = rand() % 2;
        skier->usage_count = 0;

        if (pthread_create(&skier_threads[i], NULL, skier_thread, skier) != 0) {
            perror_exit("Nie udało się utworzyć wątku dla narciarza");
        }

        usleep(rand() % 500000); // Losowy czas przybycia
    }

    // Czekanie na zakończenie wątków narciarzy
    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        pthread_join(skier_threads[i], NULL);
    }

    // Zamykanie stacji
    close_station();

    // Czekanie na zakończenie wątków pracowników
    pthread_join(worker1, NULL);
    pthread_join(worker2, NULL);

    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);

    printf("Program zakończył działanie.\n");
    return 0;
}
