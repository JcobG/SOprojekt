#ifndef TICKET_H
#define TICKET_H

#include <stdbool.h>

#define OPENING_HOUR 8
#define CLOSING_HOUR 10

// Struktura biletu
typedef struct {
    int ticket_id;      // ID karnetu
    int usage_count;    // Liczba wykorzystanych przejazdów
    bool is_vip;        // Czy bilet jest VIP?
    int expiry_time;    // Czas ważności karnetu w minutach
} Ticket;

// Funkcje związane z biletami
Ticket* purchase_ticket(int skier_id, int age);

#endif