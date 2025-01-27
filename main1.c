#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#define MAX_CHAIRS 40
#define MAX_SKIER_COUNT 12
#define MAX_TICKETS 10
#define MAX_PEOPLE_ON_PLATFORM 50
#define SIMULATION_DURATION 1 // Czas symulacji w sekundach
#define MAX_ACTIVE_THREADS 10  // Maksymalna liczba aktywnych wątków naraz

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
    int total_skiers;
    int children_4_8;
} Statistics;

Ticket tickets[MAX_TICKETS];
sem_t platform_sem;
sem_t chairlift_sem;
sem_t ticket_office_sem;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
Statistics stats = {0};
volatile bool is_lift_running = true;
volatile bool is_station_open = true;
volatile int active_threads = 0;  // Liczba aktywnych wątków

void perror_exit(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

bool is_ticket_valid(Ticket* ticket) {
    return time(NULL) < ticket->expiry_time;
}

void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;

    if (skier->age < 4) {
        printf("Narciarz #%d jest zbyt młody na korzystanie ze stacji.\n", skier->skier_id);
        free(skier);
        return NULL;
    }

    if (skier->age >= 4 && skier->age <= 8 && !skier->is_guardian) {
        printf("Dziecko #%d w wieku 4-8 lat nie ma opiekuna!\n", skier->skier_id);
        free(skier);
        return NULL;
    }

    if (skier->children_count > 2) {
        printf("Narciarz #%d nie może opiekować się więcej niż dwójką dzieci.\n", skier->skier_id);
        free(skier);
        return NULL;
    }

    while (is_ticket_valid(skier->ticket)) {
        int gate = skier->ticket->is_vip ? 0 : rand() % 4;

        if (sem_wait(&ticket_office_sem) == -1) {
            perror("Błąd sem_wait na ticket_office_sem");
            break;
        }
        sem_post(&ticket_office_sem);

        if (sem_wait(&platform_sem) == -1) {
            perror("Błąd sem_wait na platform_sem");
            break;
        }

        pthread_mutex_lock(&stats_mutex);
        stats.gate_count[gate]++;
        stats.total_skiers++;
        if (skier->age >= 4 && skier->age <= 8) {
            stats.children_4_8++;
        }
        pthread_mutex_unlock(&stats_mutex);

        printf("Narciarz #%d wszedł przez bramkę #%d.\n", skier->skier_id, gate);

        if (sem_wait(&chairlift_sem) == -1) {
            perror("Błąd sem_wait na chairlift_sem");
            break;
        }
        printf("Narciarz #%d wsiadł na krzesełko.\n", skier->skier_id);
        usleep(200000); // Skrócony czas symulacji przejazdu (200ms)
        sem_post(&chairlift_sem);

        int slope = rand() % 3;
        usleep((slope + 1) * 200000); // Skrócony czas zjazdu

        pthread_mutex_lock(&stats_mutex);
        stats.slope_count[slope]++;
        skier->ticket->usage_count++;
        pthread_mutex_unlock(&stats_mutex);

        printf("Narciarz #%d zjechał trasą #%d.\n", skier->skier_id, slope);
        sem_post(&platform_sem);

        usleep(rand() % 500000); // Symulacja odpoczynku między zjazdami
    }

    printf("Narciarz #%d zakończył korzystanie z karnetu.\n", skier->skier_id);
    free(skier);

    // Zmniejszamy licznik aktywnych wątków
    __sync_fetch_and_sub(&active_threads, 1);
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
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = 0;

    sa2.sa_handler = handle_sigusr2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;

    if (sigaction(SIGUSR1, &sa1, NULL) == -1) {
        perror_exit("Błąd ustawienia handlera SIGUSR1");
    }
    if (sigaction(SIGUSR2, &sa2, NULL) == -1) {
        perror_exit("Błąd ustawienia handlera SIGUSR2");
    }
}

void init_tickets() {
    for (int i = 0; i < MAX_TICKETS; i++) {
        tickets[i].ticket_id = i + 1;
        tickets[i].usage_count = 0;
        tickets[i].is_vip = rand() % 2;
        tickets[i].expiry_time = time(NULL) + (rand() % 3 + 1) * 3600;
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

    fprintf(report_file, "\nStatystyki narciarzy:\n");
    fprintf(report_file, "  Łączna liczba narciarzy: %d\n", stats.total_skiers);
    fprintf(report_file, "  Liczba dzieci w wieku 4-8 lat: %d\n", stats.children_4_8);
    fclose(report_file);
    printf("Raport dzienny zapisano do pliku 'daily_report.txt'.\n");
}

void close_station() {
    printf("Stacja jest zamknięta. Przetransportowanie ostatnich osób...\n");
    while (sem_trywait(&platform_sem) == 0) {
        usleep(200000); // Skrócony czas transportu
    }
    printf("Wszystkie osoby zostały przetransportowane. Wyłączanie kolejki linowej.\n");
    is_station_open = false;
}

int main() {
    srand(time(NULL));
    init_tickets();

    if (sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM) == -1) {
        perror_exit("Błąd inicjalizacji semafora platform_sem");
    }
    if (sem_init(&chairlift_sem, 0, MAX_CHAIRS) == -1) {
        perror_exit("Błąd inicjalizacji semafora chairlift_sem");
    }
    if (sem_init(&ticket_office_sem, 0, 1) == -1) {
        perror_exit("Błąd inicjalizacji semafora ticket_office_sem");
    }

    setup_signal_handlers();

    printf("Stacja narciarska jest otwarta!\n");

    time_t simulation_end_time = time(NULL) + SIMULATION_DURATION;

    pthread_t threads[MAX_SKIER_COUNT];

    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        if (time(NULL) >= simulation_end_time) {
            printf("Czas symulacji upłynął. Kończenie tworzenia nowych narciarzy.\n");
            break;
        }

        // Czekamy, jeśli aktywnych wątków jest już MAX_ACTIVE_THREADS
        while (active_threads >= MAX_ACTIVE_THREADS) {
            usleep(50000);  // Czekamy 50 ms, aby dać czas na zakończenie wątków
        }

        Skier* skier = (Skier*)malloc(sizeof(Skier));
        if (!skier) {
            perror_exit("Błąd alokacji pamięci dla narciarza");
        }

        skier->skier_id = i + 1;
        skier->age = rand() % 75 + 4;
        skier->is_guardian = (skier->age >= 18 && skier->age <= 65 && rand() % 2);
        skier->children_count = skier->is_guardian ? rand() % 3 : 0;
        skier->ticket = &tickets[rand() % MAX_TICKETS];

        // Zwiększamy licznik aktywnych wątków
        __sync_fetch_and_add(&active_threads, 1);

        if (pthread_create(&threads[i], NULL, skier_thread, skier) != 0) {
            perror_exit("Nie udało się utworzyć wątku dla narciarza");
        }

        usleep(rand() % 100000); // Symulacja odpoczynku
    }

    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }

    close_station();
    generate_daily_report();

    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);
    sem_destroy(&ticket_office_sem);

    printf("Stacja narciarska zakończyła pracę na dziś.\n");
    return 0;
}
