#include "ticket.h"
#include <stdlib.h>
#include <stdio.h>

// Zakup biletu
Ticket* purchase_ticket(int skier_id, int age) {
    Ticket* ticket = malloc(sizeof(Ticket));
    if (!ticket) { // Sprawdzenie alokacji
        fprintf(stderr, "Blad: Nie udalo sie przydzielic pamieci dla biletu narciarza #%d.\n", skier_id);
        return NULL; // 
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