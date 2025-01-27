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
#define MAX_TICKETS 200
#define MAX_PEOPLE_ON_PLATFORM 50

typedef struct {
    int ticket_id;
    int usage_count;
    bool is_vip;
    time_t expiry_time;
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

bool is_ticket_valid(Ticket* ticket) {
    return time(NULL) < ticket->expiry_time;
}

void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;
    int gate = skier->ticket->is_vip ? 0 : rand() % 4;

    sem_wait(&ticket_office_sem);

    if (skier->age < 4) {
        printf("Narciarz #%d jest zbyt młody na korzystanie ze stacji.\n", skier->skier_id);
        sem_post(&ticket_office_sem);
        free(skier);
        return NULL;
    }

    if (skier->children_count > 2) {
        printf("Narciarz #%d nie może opiekować się więcej niż dwójką dzieci.\n", skier->skier_id);
        sem_post(&ticket_office_sem);
        free(skier);
        return NULL;
    }

    if (!is_ticket_valid(skier->ticket)) {
        printf("Karnet narciarza #%d jest nieważny.\n", skier->skier_id);
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
        tickets[i].expiry_time = time(NULL) + (rand() % 3 + 1) * 3600; // Karnet ważny od 1 do 3 godzin
    }
}

void generate_daily_report() {
    FILE* report_file = fopen("daily_report.txt", "w");
    if (!report_file) {
        perror("Nie udało się otworzyć pliku z raportem");
        return;
    }

    fprintf(report_file, "=== Raport dzienny stacji narciarskiej ===\n\n");
    fprintf(report_file, "Użycie bramek:\n");
    for (int i = 0; i < 4; i++) {
        fprintf(report_file, "  Bramka #%d: %d wejść\n", i, stats.gate_count[i]);
    }

    fprintf(report_file, "\nUżycie tras zjazdowych:\n");
    for (int i = 0; i < 3; i++) {
        fprintf(report_file, "  Trasa #%d: %d zjazdów\n", i, stats.slope_count[i]);
    }

    fprintf(report_file, "\nStatystyki karnetów:\n");
    for (int i = 0; i < MAX_TICKETS; i++) {
        fprintf(report_file, "  Karnet #%d (VIP: %s): %d użyć, ważny do: %s",
                tickets[i].ticket_id,
                tickets[i].is_vip ? "TAK" : "NIE",
                tickets[i].usage_count,
                ctime(&tickets[i].expiry_time));
    }

    fclose(report_file);
    printf("Raport dzienny zapisano do pliku 'daily_report.txt'.\n");
}

int main() {
    srand(time(NULL));
    init_tickets();

    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS);
    sem_init(&ticket_office_sem, 0, 1);

    pthread_t threads[MAX_SKIER_COUNT];
    setup_signal_handlers();

    printf("Stacja narciarska jest otwarta!\n");

    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        if (!is_lift_running) {
            sleep(1);
            continue;
        }

        Skier* skier = (Skier*)malloc(sizeof(Skier));
        skier->skier_id = i + 1;
        skier->age = rand() % 75 + 4; // Wiek narciarza od 4 do 79 lat
        skier->is_guardian = (skier->age >= 18 && skier->age <= 65 && rand() % 2);
        skier->children_count = skier->is_guardian ? rand() % 3 : 0; // Do dwóch dzieci
        skier->ticket = &tickets[rand() % MAX_TICKETS];

        if (pthread_create(&threads[i], NULL, skier_thread, skier) != 0) {
            perror_exit("Nie udało się utworzyć wątku dla narciarza");
        }

        usleep(rand() % 500000); // Czas pojawienia się kolejnego narciarza
    }

    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    generate_daily_report();

    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);
    sem_destroy(&ticket_office_sem);

    printf("Stacja narciarska zakończyła pracę na dziś.\n");
    return 0;
} 
