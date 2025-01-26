#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define MAX_CHAIRS 40
#define MAX_SKIER_COUNT 120
#define MAX_TICKETS 200
#define MAX_PEOPLE_ON_PLATFORM 50

typedef struct {
    int ticket_id;
    int usage_count;
    bool is_vip;
} Ticket;

typedef struct {
    int skier_id;
    int age;
    bool is_guardian;
    int children_count;
    Ticket* ticket;
} Skier;

typedef struct {
    int gate_count[4];
    int slope_count[3];
} Statistics;

Ticket tickets[MAX_TICKETS];
sem_t platform_sem;
sem_t chairlift_sem;
sem_t ticket_office_sem;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
Statistics stats;
volatile bool is_lift_running = true;

void perror_exit(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;
    int gate = rand() % 4;

    sem_wait(&ticket_office_sem);
    if (skier->age < 4) {
        printf("Narciarz #%d jest zbyt młody na korzystanie ze stacji.\n", skier->skier_id);
        sem_post(&ticket_office_sem);
        free(skier);
        return NULL;
    }

    if (skier->children_count > 0 && skier->children_count > 2) {
        printf("Narciarz #%d nie może opiekować się więcej niż dwójką dzieci.\n", skier->skier_id);
        sem_post(&ticket_office_sem);
        free(skier);
        return NULL;
    }

    sem_post(&ticket_office_sem);
    sem_wait(&platform_sem);

    pthread_mutex_lock(&stats_mutex);
    stats.gate_count[gate]++;
    pthread_mutex_unlock(&stats_mutex);

    printf("Narciarz #%d wszedł przez bramkę #%d.\n", skier->skier_id, gate);

    sem_wait(&chairlift_sem);
    printf("Narciarz #%d wsiadł na krzesełko.\n", skier->skier_id);

    sleep(2); // Symulacja jazdy krzesełkiem

    sem_post(&chairlift_sem);

    int slope = rand() % 3;
    sleep(slope + 1); // Symulacja zjazdu po trasie

    pthread_mutex_lock(&stats_mutex);
    stats.slope_count[slope]++;
    skier->ticket->usage_count++;
    pthread_mutex_unlock(&stats_mutex);

    printf("Narciarz #%d zjechał trasą #%d.\n", skier->skier_id, slope);

    sem_post(&platform_sem);
    free(skier);
    return NULL;
}

void handle_sigusr1(int sig) {
    printf("Otrzymano sygnał SIGUSR1: Zatrzymanie kolejki linowej.\n");
    is_lift_running = false;
}

void handle_sigusr2(int sig) {
    printf("Otrzymano sygnał SIGUSR2: Wznowienie kolejki linowej.\n");
    is_lift_running = true;
}

void setup_signal_handlers() {
    struct sigaction sa1, sa2;
    sa1.sa_handler = handle_sigusr1;
    sa2.sa_handler = handle_sigusr2;
    sigaction(SIGUSR1, &sa1, NULL);
    sigaction(SIGUSR2, &sa2, NULL);
}

void init_tickets() {
    for (int i = 0; i < MAX_TICKETS; i++) {
        tickets[i].ticket_id = i + 1;
        tickets[i].usage_count = 0;
        tickets[i].is_vip = rand() % 2;
    }
}

void generate_report() {
    printf("\nPodsumowanie dnia:\n");

    for (int i = 0; i < 4; i++) {
        printf("Bramka #%d: %d wejść.\n", i, stats.gate_count[i]);
    }

    for (int i = 0; i < 3; i++) {
        printf("Trasa #%d: %d zjazdów.\n", i, stats.slope_count[i]);
    }

    printf("\nStatystyki karnetów:\n");
    for (int i = 0; i < MAX_TICKETS; i++) {
        if (tickets[i].usage_count > 0) {
            printf("Karnet #%d: użyty %d razy.\n", tickets[i].ticket_id, tickets[i].usage_count);
        }
    }
}

int main() {
    srand(time(NULL));
    init_tickets();
    setup_signal_handlers();

    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS);
    sem_init(&ticket_office_sem, 0, 1);

    pthread_t threads[MAX_SKIER_COUNT];
    int skier_count = 50;

    for (int i = 0; i < skier_count; i++) {
        Skier* skier = malloc(sizeof(Skier));
        skier->skier_id = i + 1;
        skier->age = rand() % 70 + 4;
        skier->is_guardian = rand() % 2;
        skier->children_count = skier->is_guardian ? rand() % 3 : 0;
        skier->ticket = &tickets[rand() % MAX_TICKETS];

        pthread_create(&threads[i], NULL, skier_thread, skier);
        usleep(100000);
    }

    for (int i = 0; i < skier_count; i++) {
        pthread_join(threads[i], NULL);
    }

    generate_report();

    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);
    sem_destroy(&ticket_office_sem);

    printf("Symulacja zakończona.\n");
    return 0;
}
