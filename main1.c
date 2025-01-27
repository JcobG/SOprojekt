#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>

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
#define NUM_GATES 4 // Liczba bramek

// Symulowany czas
volatile int simulated_time = 0; 
volatile bool is_station_open = true;
volatile bool is_lift_running = true;
volatile int people_on_lift = 0;
volatile int skiers_on_platform = 0;
volatile int skiers_in_lift_queue = 0;
// Semafory
sem_t platform_sem;
sem_t chairlift_sem;
sem_t lift_control_sem;
sem_t vip_chairlift_sem; // Semafor dla osób VIP
sem_t gates[NUM_GATES]; // Semafory dla bramek

sem_t gate_ready[NUM_GATES]; // Semafory wskazujące, że bramka jest gotowa


// Mutexy i zmienne warunkowe
pthread_mutex_t lift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t station_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t lift_condition = PTHREAD_COND_INITIALIZER;
pthread_mutex_t chairlift_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lift_operation_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex do synchronizacji operacji

pthread_t worker_thread_id,responder_thread_id, time_thread, lift_shutdown,skier_thread_id;
pthread_t gate_threads[NUM_GATES];
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
    bool has_guardian;  //Czy ma opiekuna?
    int other_guarded_children_count; // Liczba dzieci pod opieką
    Ticket* ticket;     // Wskaźnik na bilet
    bool is_child;      // Czy narciarz jest dzieckiem
} Skier;

// Wskaźnik do pamięci dzielonej
int* shared_usage;
int shm_id; // Globalna zmienna dla ID pamięci dzielonej
// ID kolejki komunikatów
int msgid;

// Zakup biletu
Ticket* purchase_ticket(int skier_id, int age) {
    Ticket* ticket = malloc(sizeof(Ticket));
    if (!ticket) { // Sprawdzenie alokacji
        fprintf(stderr, "Błąd: Nie udało się przydzielić pamięci dla biletu narciarza #%d.\n", skier_id);
        return NULL; // Zwrócenie NULL, aby zgłosić błąd
    }

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
        printf("\033[33mNarciarz #%d otrzymal znizke na karnet.\n\033[0m", skier_id);
    }

    return ticket;
}

// Funkcja zatrzymująca kolejkę linową
void stop_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (is_lift_running) {
        is_lift_running = false;
        printf("\033[43mKolejka linowa zostala zatrzymana przez pracownika #%d.\033[0m\n",worker_id);
        pthread_cond_broadcast(&lift_condition); // Powiadomienie wszystkich o zatrzymaniu
    }
    pthread_mutex_unlock(&lift_mutex);
}

void resume_lift(int worker_id) {
    pthread_mutex_lock(&lift_mutex);
    if (!is_lift_running) {
        is_lift_running = true;
        printf("\033[43mKolejka linowa zostala wznowiona przez pracownika #%d.\033[0m\n", worker_id);
        pthread_cond_broadcast(&lift_condition); // Powiadomienie wszystkich o wznowieniu
    }
    pthread_mutex_unlock(&lift_mutex);
}

void* gate_thread(void* arg) {
    int gate_id = *(int*)arg;
    free(arg);

    while (true) {

        sem_wait(&gates[gate_id]); // Oczekiwanie na narciarza przy bramce
        pthread_mutex_lock(&station_mutex);
        if (!is_station_open) {
            pthread_mutex_unlock(&station_mutex);
            break;
        }
        pthread_mutex_unlock(&station_mutex);

        printf("\033[34m[Bramka #%d] Narciarz przechodzi przez bramke na dolny peron.\n\033[0m", gate_id);
        sem_post(&gate_ready[gate_id]);
	sem_post(&platform_sem); // Zwiększenie liczby osób na peronie
    }

    printf("\033[34m[Bramka #%d] Zakonczyla prace.\n\033[0m", gate_id);
    return NULL;
}

// Symulacja czasu
void* time_simulation_thread(void* arg) {
    int simulation_duration = (CLOSING_HOUR - OPENING_HOUR) * 60;

    while (simulated_time < simulation_duration) {
        sleep(SIMULATION_STEP);
        simulated_time += 2; // 1 sekunda = 2 minuty
        if (simulated_time % 60 == 0) {
            printf("Symulowany czas: %d h.\n", simulated_time / 60);
        }
    }

    pthread_mutex_lock(&station_mutex);
    is_station_open = false;
    pthread_mutex_unlock(&station_mutex);

    printf("\033[31mStacja zamyka sie.\n\033[0m");
    sleep(5);  // Czas na wylaczenie kolejki

    return NULL;
}

// Funkcja obslugi dzieci i opiekunów
bool can_ski(Skier* skier) {
    if (skier->is_child) {
        if (!skier->has_guardian) {
            printf("Narciarz #%d to dziecko, ktore nie ma opiekuna i nie może korzystać z kolejki.\n", skier->skier_id);
            return false;
        } else if (skier->other_guarded_children_count >= 2){ // Sprawdzanie liczby dzieci pod opieka
            printf("Narciarz #%d nie moze wejsc na kolejke, bo jego opiekun ma juz pod opieka maksymalna liczbe dzieci,\n",skier->skier_id);
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
         if (!is_station_open) break;
	// Oczekiwanie na miejsce na platformie
	 int gate_id = rand() % NUM_GATES;
	 printf("\033[42mNarciarz #%d wchodzi przez bramke #%d.\033[0m\n", skier->skier_id, gate_id);
        sem_post(&gates[gate_id]); // Wysłanie narciarza do bramki
	sem_wait(&gate_ready[gate_id]);
        pthread_mutex_lock(&station_mutex);
        if (!is_station_open) {
            printf("Narciarz #%d nie moze wejsc na platformee, stacja jest zamknieta.\n", skier->skier_id);
            pthread_mutex_unlock(&station_mutex);
            break;
        }
        pthread_mutex_unlock(&station_mutex);
 	 sem_wait(&platform_sem);   // Oczekiwanie na wejście na platformę

        __sync_add_and_fetch(&skiers_on_platform, 1); // Zwiększenie liczby narciarzy na platformie
        printf("\033[42mNarciarz #%d wchodzi na platforme.\033[0m\n", skier->skier_id);

        pthread_mutex_lock(&lift_mutex);
        while (!is_lift_running) { // Czekaj na wznowienie kolejki
            printf("Narciarz #%d czeka na wznowienie kolejki.\n", skier->skier_id);
            pthread_cond_wait(&lift_condition, &lift_mutex);
        }
        pthread_mutex_unlock(&lift_mutex);


	// VIP ma pierwszenstwo
        if (skier->ticket->is_vip) {
            printf("\033[36mNarciarz VIP #%d ma pierwszenstwo i wsiada na krzeselko.\n\033[0m", skier->skier_id);
            sem_wait(&vip_chairlift_sem); // VIP korzysta z dedykowanego semafora
        } else {
            printf("Narciarz #%d czeka na krzeselko.\n", skier->skier_id);
            sem_wait(&chairlift_sem); // Osoby bez VIP czekaja w zwyklej kolejce
        }

        // Narciarz wsiada na krzeselko
//        sem_wait(&chairlift_sem);
        __sync_add_and_fetch(&skiers_in_lift_queue, 1); // Zwiekszenie liczby narciarzy w kolejce
        pthread_mutex_lock(&lift_operation_mutex);
        while (!is_lift_running) {
            printf("Narciarz #%d czeka na wznowienie kolejki przed wsiadaniem.\n", skier->skier_id);
            pthread_cond_wait(&lift_condition, &lift_operation_mutex);
        }
	if(!skier->ticket->is_vip){
        printf("\033[42mNarciarz #%d wsiada na krzeselko.\033[0m\n", skier->skier_id);
        }
	pthread_mutex_unlock(&lift_operation_mutex);

        // Symulacja jazdy
	int ride_time = 5;  
      for (int t = 0; t < ride_time; t++) {
            pthread_mutex_lock(&lift_operation_mutex);
            while (!is_lift_running) {
                printf("Narciarz #%d zatrzymuje sie na krzeselku i czeka na wznowienie.\n", skier->skier_id);
                pthread_cond_wait(&lift_condition, &lift_operation_mutex);
            }
            pthread_mutex_unlock(&lift_operation_mutex);
            sleep(1); // Symulacja jednej sekundy jazdy
        }

        // Narciarz konczy jazdę
        pthread_mutex_lock(&lift_operation_mutex);
        while (!is_lift_running) {
            printf("Narciarz #%d czeka na wznowienie kolejki przed zejsciem.\n", skier->skier_id);
            pthread_cond_wait(&lift_condition, &lift_operation_mutex);
        }
        printf("Narciarz #%d konczy jazde krzeselkiem i schodzi z platformy.\n", skier->skier_id);
        pthread_mutex_unlock(&lift_operation_mutex);

        skier->ticket->usage_count++;

        // Zapis zjazdu w pamięci dzielonej
        shared_usage[skier->skier_id]++;
        sem_post(&chairlift_sem); // Zwolnienie miejsca na krzesełku
        sem_post(&platform_sem);  // Zwolnienie miejsca na platformie

        __sync_sub_and_fetch(&skiers_on_platform, 1); // Zmniejszenie liczby narciarzy na platformie
        __sync_sub_and_fetch(&skiers_in_lift_queue, 1); // Zmniejszenie liczby narciarzy w kolejce

        // Wybor trasy i czas przejazdu
        int track_choice = rand() % 3;  // Wybor trasy
        if (track_choice == 0) {
            sleep(T1_TIME);
            printf("Narciarz #%d zjezdza trasa T1.\n", skier->skier_id);
        } else if (track_choice == 1) {
            sleep(T2_TIME);
            printf("Narciarz #%d zjezdza trasa T2.\n", skier->skier_id);
        } else {
            sleep(T3_TIME);
            printf("Narciarz #%d zjezdza trasa T3.\n", skier->skier_id);
        }
    }

    printf("Narciarz #%d konczy dzien na stacji.\n", skier->skier_id);
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
            snprintf(msg.message_text, sizeof(msg.message_text), "\033[33mPracownik #%d zatrzymuje kolejke.\033[0m", worker_id);

            stop_lift(worker_id);

            printf("\033[33mPracownik #%d wysyla zapytanie do pracownika #%d o gotowosc.\n\033[0m", worker_id, 2);
            msgsnd(msgid, &msg, sizeof(msg), 0);

            msgrcv(msgid, &msg, sizeof(msg), 2, 0); // Czekaj na odpowiedz od Pracownika 2
            printf("\033[33mPracownik #%d otrzymal odpowiedz: %s\n\033[0m", worker_id, msg.message_text);

            resume_lift(worker_id);
        }
        sleep(1); // Czas pracy pracownika
    }

    printf("\033[33m[Pracownik #%d] Konczy prace.\033[0m\n", worker_id);
    return NULL;
}
// Zakonczenie pracy kolejki po ostatnim narciarzu
void* lift_shutdown_thread(void* arg) {
    while (is_station_open || skiers_on_platform > 0 || skiers_in_lift_queue > 0) {
        sleep(1); // Sprawdzaj co sekunde
    }

    printf("Ostatni narciarz opuscil platforme. Kolejka zatrzymuje sie za 5 sekund.\n");
    sleep(5); // Czas na zatrzymanie kolejki

    pthread_mutex_lock(&lift_mutex);
    is_lift_running = false;
    pthread_cond_broadcast(&lift_condition);
    pthread_mutex_unlock(&lift_mutex);

    printf("\033[41mKolejka zostala zatrzymana.\033[0m\n");
    return NULL;
}
void* responder_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (is_station_open) {
        Message msg;
        msgrcv(msgid, &msg, sizeof(msg), 1, 0);

        // Sprawdz, czy to sygnal zakonczenia
        if (strcmp(msg.message_text, "END") == 0) {
            printf("[Pracownik #%d] Konczy prace na podstawie sygnalu zakonczenia.\n", worker_id);
            break;
        }

        printf("\033[33mmPracownik #%d otrzymal komunikat: %s\n\033[0m", worker_id, msg.message_text);
        sleep(2); // Symulacja sprawdzania gotowosci

        msg.message_type = 2; // Odpowiedz do Pracownika 1
        snprintf(msg.message_text, sizeof(msg.message_text), "\033[33mPracownik #%d gotowy do wznowienia.\033[0m", worker_id);
        msgsnd(msgid, &msg, sizeof(msg), 0);
    }

    return NULL;
}
void statistic_signal_handler(int signum) {
	if (signum == SIGUSR2) {
   	 printf("\n[INFO] Otrzymano sygnał SIGUSR2. Generowanie raportu diagnostycznego...\n");
   	 printf("[Raport diagnostyczny]\n");
   	 printf("- Czas symulowany: %d minut.\n", simulated_time);
   	 printf("- Narciarze na platformie: %d\n", skiers_on_platform);
   	 printf("- Narciarze w kolejce: %d\n", skiers_in_lift_queue);          
         }
}
void cleanup(int signum) {

    printf("\nZatrzymano program. Zwolnienie zasobow...\n");
pthread_mutex_lock(&station_mutex);
    is_station_open = false;
    pthread_mutex_unlock(&station_mutex);



    // Odblokowanie wszystkich wątków oczekujących na semafory
    for (int i = 0; i < NUM_GATES; i++) {
        sem_post(&gates[i]);
    }
    pthread_cond_broadcast(&lift_condition);

    // Czekanie na zakończenie pracy głównych wątków
    pthread_cancel(time_thread);
    pthread_join(time_thread, NULL);

    pthread_cancel(lift_shutdown);
    pthread_join(lift_shutdown, NULL);

    pthread_cancel(worker_thread_id);
    pthread_join(worker_thread_id, NULL);

    pthread_cancel(responder_thread_id);
    pthread_join(responder_thread_id, NULL);

    // Czekanie na zakończenie wątków bramek
    for (int i = 0; i < NUM_GATES; i++) {
        pthread_cancel(gate_threads[i]);
        pthread_join(gate_threads[i], NULL);
    }    

        // Zwolnienie pami   ^yci dzielonej
    if (shmdt(shared_usage) == -1) {
        fprintf(stderr, "Blad: Nie udalo sie odlaczyc pamieci dzielonej.\n");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Blad: Nie udalo sie usunac segmentu pamieci dzielonej.\n");
    }

    // Usuni   ^ycie kolejki komunikat      w
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Blad: Nie udalo sie usunac kolejki komunikato w.\n");
    }


    // Zwalnianie semaforów
    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);
    sem_destroy(&lift_control_sem);
    sem_destroy(&vip_chairlift_sem);
    for (int i = 0; i < NUM_GATES; i++) {
        sem_destroy(&gates[i]);
        sem_destroy(&gate_ready[i]);
    }

    printf("Zasoby zostaly zwolnione. Program zakonczyl dzialanie.\n");
    exit(EXIT_SUCCESS);
}

int main() {
	signal(SIGINT, cleanup);
    signal(SIGUSR2, statistic_signal_handler); 
	srand(time(NULL));
// Inicjalizacja semaforów dla bramek z obsługą błędów
for (int i = 0; i < NUM_GATES; i++) {
    if (sem_init(&gates[i], 0, 0) == -1) {
        fprintf(stderr, "Blad: Nie udalo sie zainicjalizowac semafora dla bramki #%d.\n", i);
        // Zwalnianie wczesniej zainicjalizowanych semaforów
        for (int j = 0; j < i; j++) {
            sem_destroy(&gates[j]);
        }
        exit(EXIT_FAILURE);
    }

    if (sem_init(&gate_ready[i], 0, 0) == -1) {
        fprintf(stderr, "Błąd: Nie udało się zainicjalizować semafora 'gate_ready' dla bramki #%d.\n", i);
        // Zwalnianie wcześniej zainicjalizowanych semaforów
        sem_destroy(&gates[i]);
        for (int j = 0; j < i; j++) {
            sem_destroy(&gates[j]);
            sem_destroy(&gate_ready[j]);
        }
        exit(EXIT_FAILURE);
    }
}


    // Inicjalizacja semaforów z obsługą błędów
if (sem_init(&platform_sem, 0, MAX_PEOPLE_ON_PLATFORM) == -1) {
    fprintf(stderr, "Błąd: Nie udało się zainicjalizować semafora platformy.\n");
    exit(EXIT_FAILURE);
}

if (sem_init(&chairlift_sem, 0, MAX_CHAIRS * MAX_PEOPLE_ON_CHAIR) == -1) {
    fprintf(stderr, "Błąd: Nie udało się zainicjalizować semafora kolejki linowej.\n");
    sem_destroy(&platform_sem); // Usunięcie już zainicjalizowanego semafora
    exit(EXIT_FAILURE);
}

// Inicjalizacja semafora lift_control_sem z obsługą błędów
if (sem_init(&lift_control_sem, 0, 1) == -1) {
    fprintf(stderr, "Błąd: Nie udało się zainicjalizować semafora lift_control_sem.\n");
    exit(EXIT_FAILURE);
}

// Inicjalizacja semafora vip_chairlift_sem z obsługą błędów
if (sem_init(&vip_chairlift_sem, 0, MAX_CHAIRS * MAX_PEOPLE_ON_CHAIR) == -1) {
    fprintf(stderr, "Błąd: Nie udało się zainicjalizować semafora vip_chairlift_sem.\n");
    sem_destroy(&lift_control_sem); // Zwalnianie wcześniejszego semafora
    exit(EXIT_FAILURE);
}

    // Inicjalizacja pamięci dzielonej
    int shm_id = shmget(IPC_PRIVATE, sizeof(int) * 1000, IPC_CREAT | 0666); // Zakładamy maksymalnie 1000 narciarzy
    if (shm_id == -1) {
        fprintf(stderr, "Błąd: Nie udało się utworzyć pamięci dzielonej.\n");
        exit(EXIT_FAILURE); // Zakończenie programu w przypadku błędu
    }    
    shared_usage = shmat(shm_id, NULL, 0);
    if (shared_usage == (void*)-1) {
        fprintf(stderr, "Błąd: Nie udało się przydzielić pamięci dzielonej.\n");
        shmctl(shm_id, IPC_RMID, NULL); // Usunięcie segmentu pamięci
        exit(EXIT_FAILURE);
    }
	

    for (int i = 0; i < 1000; i++) {
        shared_usage[i] = 0;
    }

// Inicjalizacja kolejki komunikatów z obsługą błędów
msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
if (msgid == -1) {
    fprintf(stderr, "Błąd: Nie udało się utworzyć kolejki komunikatów.\n");
    exit(EXIT_FAILURE); // Zakończenie programu w przypadku błędu
}

    pthread_t time_thread, lift_shutdown;
    pthread_create(&time_thread, NULL, time_simulation_thread, NULL);
pthread_create(&lift_shutdown, NULL, lift_shutdown_thread, NULL);
    
// Tworzenie wątków dla bramek
    pthread_t gate_threads[NUM_GATES];
    for (int i = 0; i < NUM_GATES; i++) {
        int* gate_id = malloc(sizeof(int));
	if (!gate_id) {
    		fprintf(stderr, "Błąd: Nie udało się przydzielić pamięci dla identyfikatora bramki.\n");
    		exit(EXIT_FAILURE);
	}
        *gate_id = i;
	if (pthread_create(&gate_threads[i], NULL, gate_thread, gate_id) != 0) {
    		fprintf(stderr, "Błąd: Nie udało się utworzyć wątku dla bramki #%d.\n", i);
    		free(gate_id); // Zwolnienie pamięci w przypadku błędu
    		exit(EXIT_FAILURE);
	}
    }



     // Tworzenie wątków pracowników
    pthread_t worker_thread_id, responder_thread_id;
    int* worker_id = malloc(sizeof(int));
    if (!worker_id) {
	    fprintf(stderr, "Błąd: Nie udało się przydzielić pamięci dla identyfikatora pracownika.\n");
	    exit(EXIT_FAILURE);
	}
	*worker_id = 1;
	if (pthread_create(&worker_thread_id, NULL, worker_thread, worker_id) != 0) {
	    fprintf(stderr, "Błąd: Nie udało się utworzyć wątku dla pracownika.\n");
	    free(worker_id); // Zwolnienie pamięci w przypadku błędu
	    exit(EXIT_FAILURE);
	}

    int* responder_id = malloc(sizeof(int));
    if (!responder_id) {
    fprintf(stderr, "Błąd: Nie udało się przydzielić pamięci dla identyfikatora pracownika.\n");
    exit(EXIT_FAILURE);
	}
	*responder_id = 2;
	if (pthread_create(&responder_thread_id, NULL, responder_thread, responder_id) != 0) {
	    fprintf(stderr, "Błąd: Nie udało się utworzyć wątku dla respondenta.\n");
	    free(responder_id); // Zwolnienie pamięci w przypadku błędu
	    exit(EXIT_FAILURE);
	}
    // Tworzenie wątków narciarzy w nieskończonej pętli
    int skier_id = 0;
    while (is_station_open) {
        Skier* skier = malloc(sizeof(Skier));
    	if (!skier) { // Sprawdzenie alokacji
        	fprintf(stderr, "Błąd: Nie udało się przydzielić pamięci dla narciarza.\n");
        	return NULL;
    	}
   	skier->skier_id = skier_id;
        skier->age = rand() % 75 + 4;
        skier->ticket = purchase_ticket(skier->skier_id, skier->age);
        if (!skier->ticket) {
 	 fprintf(stderr, "Błąd: Nie udało się utworzyć biletu dla narciarza #%d.\n", skier->skier_id);
  	 free(skier); // Zwolnienie pamięci w przypadku błędu
   	 continue; // Przejście do następnego narciarza
	}
	skier->is_guardian = skier->age >= 18 && skier->age <= 65;
        skier->is_child = skier->age >= 4 && skier->age <= 8;
	skier->has_guardian = skier->is_child ? (rand() % 2) : -1;
	skier->other_guarded_children_count = (skier->skier_id > 0) ? (rand() % skier->skier_id) : 0;
        pthread_t skier_thread_id;
        pthread_create(&skier_thread_id, NULL, skier_thread, skier);
 int sleep_time_ms = (rand() % 3000) + 500; // Od 500 ms do 3 sekund
    usleep(sleep_time_ms * 1000); // Zamiana milisekund na mikrosekundy
        skier_id++;
    }
    pthread_join(time_thread, NULL);
    pthread_join(lift_shutdown, NULL);

    // Wysłanie sygnału zakończenia do responder_thread
    Message end_msg;
    end_msg.message_type = 1;
    snprintf(end_msg.message_text, sizeof(end_msg.message_text), "END");
    msgsnd(msgid, &end_msg, sizeof(end_msg), 0);
    pthread_join(worker_thread_id, NULL);
    pthread_join(responder_thread_id, NULL);
    pthread_join(skier_thread_id, NULL);
        // Zakonczenie pracy bramek
for (int i = 0; i < NUM_GATES; i++) {
    sem_post(&gates[i]); // Odblokowanie semafor      w bramek
}
	

    // Wyświetlenie raportu po zakończeniu wszystkich wątków
	sleep(5);
    printf("\n[Raport dzienny z pamięci dzielonej]\n");
    for (int i = 0; i < skier_id; i++) {
        if (shared_usage[i] > 0) {
            printf("Narciarz #%d wykonał %d zjazdów.\n", i, shared_usage[i]);
        }
    }

    // Zakończenie wątków bramek
    for (int i = 0; i < NUM_GATES; i++) {
        pthread_join(gate_threads[i], NULL);
        sem_destroy(&gates[i]);
	    sem_destroy(&gate_ready[i]); 
    }
    // Zwolnienie pamięci dzielonej
    //shmdt(shared_usage);
   // shmctl(shm_id, IPC_RMID, NULL);
	
    if (shmdt(shared_usage) == -1) {
        fprintf(stderr, "Blad: Nie udało się odłączyć pamięci dzielonej.\n");
    }
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Blad: Nie udało się usunąć segmentu pamięci dzielonej.\n");
    }

// Usunięcie kolejki komunikatów
if (msgctl(msgid, IPC_RMID, NULL) == -1) {
    fprintf(stderr, "Błąd: Nie udało się usunąć kolejki komunikatów.\n");
}
    sem_destroy(&platform_sem);
    sem_destroy(&chairlift_sem);

    printf("Program zakończył działanie.\n");
    return 0;
}
