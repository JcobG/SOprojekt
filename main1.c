#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>

#define MAX_CHAIRS 40
#define MAX_SKIER_COUNT 120
#define MAX_PEOPLE_ON_PLATFORM 50
#define OPENING_HOUR 8
#define CLOSING_HOUR 10
#define SIMULATION_STEP 1 // 1 sekunda = 1 minuta w symulacji

volatile int simulated_time = 0; // Czas symulowany w minutach
volatile bool is_station_open = true;
volatile bool is_lift_running = true;
volatile int active_skiers = 0;

sem_t platform_sem;
sem_t chairlift_sem;
sem_t vip_platform_sem;

pthread_mutex_t station_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t lift_condition = PTHREAD_COND_INITIALIZER;
pthread_cond_t station_close_cond = PTHREAD_COND_INITIALIZER;

typedef struct {
    int ticket_id;
    int usage_count;
    bool is_vip;
    int expiry_time; // Czas ważności karnetu w minutach
} Ticket;

typedef struct {
    int skier_id;
    int age;
    bool is_vip;
    bool is_guardian;
    int children_count;
    int guardian_id;
    Ticket* ticket;
    bool is_child;
} Skier;

void perror_exit(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

Ticket* purchase_ticket(int skier_id, int age) {
    Ticket* ticket = malloc(sizeof(Ticket));
    ticket->ticket_id = skier_id;
    ticket->usage_count = 0;

    int ticket_type = rand() % 4;
    if (ticket_type == 3) {
        ticket->expiry_time = (CLOSING_HOUR - OPENING_HOUR) * 60; // Dzienny
    } else {
        ticket->expiry_time = (ticket_type + 1) * 60; // Tk1, Tk2, Tk3
    }

    ticket->is_vip = rand() % 5 == 0; // 20% szans na VIP

    if (age < 12 || age > 65) {
        printf("Narciarz #%d otrzymał zniżkę na karnet.\n", skier_id);
    }

    return ticket;
}

void stop_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (is_lift_running) {
        is_lift_running = false;
        printf("Kolejka linowa została zatrzymana przez pracownika #%d.\n", worker_id);
    }
    pthread_mutex_unlock(&lift_mutex);
}

void resume_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (!is_lift_running) {
        is_lift_running = true;
        printf("Kolejka linowa została wznowiona przez pracownika #%d.\n", worker_id);
        pthread_cond_broadcast(&lift_condition);
    }
    pthread_mutex_unlock(&lift_mutex);
}

void* skier_thread(void* arg) {
    Skier* skier = (Skier*)arg;

    pthread_mutex_lock(&station_mutex);
    active_skiers++;
    pthread_mutex_unlock(&station_mutex);

    if (skier->is_child) {
        printf("Dziecko #%d w wieku 4–8 lat wymaga opiekuna.\n", skier->skier_id);
        if (skier->guardian_id == -1) {
            printf("Dziecko #%d nie ma przypisanego opiekuna i nie może wejść.\n", skier->skier_id);
            pthread_mutex_lock(&station_mutex);
            active_skiers--;
            pthread_mutex_unlock(&station_mutex);
            free(skier);
            return NULL;
        }
        printf("Dziecko #%d jest pod opieką narciarza #%d.\n", skier->skier_id, skier->guardian_id);
    }

    while (simulated_time < skier->ticket->expiry_time && is_station_open) {
        if (!skier->is_child) {
            printf("Narciarz #%d wchodzi na peron.\n", skier->skier_id);
        } else {
            printf("Dziecko #%d wchodzi na peron.\n", skier->skier_id);
        }

        if (skier->ticket->is_vip) {
            sem_wait(&vip_platform_sem);
            printf("Narciarz #%d (VIP) jest na platformie.\n", skier->skier_id);
        } else {
            sem_wait(&platform_sem);
            printf("Narciarz #%d jest na platformie.\n", skier->skier_id);
        }

        pthread_mutex_lock(&lift_mutex);
        while (!is_lift_running) {
            printf("Narciarz #%d czeka na wznowienie kolejki.\n", skier->skier_id);
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

        if (skier->ticket->is_vip) {
            sem_post(&vip_platform_sem);
        } else {
            sem_post(&platform_sem);
        }
    }

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

    while (is_station_open) {
        if (rand() % 10 == 0) {
            stop_lift(worker_id);
            sleep(2);
            resume_lift(worker_id);
        }
        sleep(1);
    }

    printf("[Pracownik #%d] Kończy pracę.\n", worker_id);
    return NULL;
}

void* time_simulation_thread(void* arg) {
    int simulation_duration = (CLOSING_HOUR - OPENING_HOUR) * 60;

    while (simulated_time < simulation_duration) {
        sleep(SIMULATION_STEP);
        simulated_time+=2;
        if (simulated_time % 60 == 0) {
            printf("Symulowany czas: %d godzin minęło.\n", simulated_time / 60);
        }
    }

    pthread_mutex_lock(&station_mutex);
    is_station_open = false;
    pthread_cond_broadcast(&lift_condition);
    pthread_mutex_unlock(&station_mutex);

    printf("Stacja zamyka się.\n");
    return NULL;
}

int main() {
    srand(time(NULL));

    sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM);
    sem_init(&vip_platform_sem, 0, 10);
    sem_init(&chairlift_sem, 0, MAX_CHAIRS);

    pthread_t skier_threads[MAX_SKIER_COUNT * 2];
    pthread_t worker1, worker2, time_thread;

    int skier_count = 0;
    int child_count = 0;
    int thread_index = 0;

    for (int i = 1; i <= 2; i++) {
        int* worker_id = malloc(sizeof(int));
        *worker_id = i;
        if (pthread_create((i == 1 ? &worker1 : &worker2), NULL, worker_thread, worker_id) != 0) {
            perror_exit("Nie udało się utworzyć wątku pracownika");
        }
    }

    if (pthread_create(&time_thread, NULL, time_simulation_thread, NULL) != 0) {
        perror_exit("Nie udało się utworzyć wątku czasu symulacji");
    }

    for (int i = 0; i < MAX_SKIER_COUNT; i++) {
        Skier* skier = malloc(sizeof(Skier));
        skier->skier_id = ++skier_count;
        skier->age = rand() % 75 + 4;
        skier->ticket = purchase_ticket(skier->skier_id, skier->age);
        skier->is_guardian = skier->age >= 18 && skier->age <= 65;
        skier->children_count = 0;
        skier->guardian_id = -1;
        skier->is_child = false;

        if (skier->is_guardian) {
            int num_children = rand() % 3;
            for (int j = 0; j < num_children; j++) {
                Skier* child = malloc(sizeof(Skier));
                child->skier_id = ++child_count;
                child->age = rand() % 5 + 4;
                child->ticket = purchase_ticket(child->skier_id, child->age);
                child->guardian_id = skier->skier_id;
                child->is_child = true;

                printf("Narciarz #%d przyjechał z dzieckiem #%d.\n", skier->skier_id, child->skier_id);

                if (pthread_create(&skier_threads[thread_index++], NULL, skier_thread, child) != 0) {
                    perror_exit("Nie udało się utworzyć wątku dla dziecka");
                }
            }
        }

        if (pthread_create(&skier_threads[thread_index++], NULL, skier_thread, skier) != 0) {
            perror_exit("Nie udało się utworzyć wątku dla narciarza");
        }

        usleep(rand() % 500000);
    }

    for (int i = 0; i < thread_index; i++) {
        pthread_join(skier_threads[i], NULL);
    }

    pthread_join(time_thread, NULL);
    pthread_join(worker1, NULL);
    pthread_join(worker2, NULL);

    sem_destroy(&platform_sem);
    sem_destroy(&vip_platform_sem);
    sem_destroy(&chairlift_sem);

    printf("Program zakończył działanie.\n");
    return 0;
}
