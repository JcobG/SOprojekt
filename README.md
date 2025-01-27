# SOprojekt
Projekt SO 2025 Stacja narciarska - Jakub Gędłek

## Opis Projektu
Ten projekt implementuje symulację działania stacji narciarskiej. Symulacja obejmuje różne aspekty, takie jak:

- Zarządzanie narciarzami, w tym obsługa VIP, dzieci i ich opiekunów.
- Funkcjonowanie bramek wejściowych i platformy.
- Zarządzanie kolejką linową i jej zatrzymywaniem oraz wznawianiem.
- Wykorzystanie mechanizmów IPC (pamięć dzielona, kolejki komunikatów, semaforów).
- Obsługa zdarzeń i sygnałów, takich jak generowanie raportu diagnostycznego i zamykanie stacji.

## Funkcjonalności
- Kolejka linowa: Obsługa narciarzy na platformie oraz VIP-ów.
- Zarządzanie bramkami: Bramy kontrolujące wejście narciarzy na platformę.
- Raporty diagnostyczne: Generowanie raportów po otrzymaniu sygnału SIGUSR2.
- Bezpieczeństwo i dodatki: Obsługa dzieci z opiekunami, zniżki na bilety.
- Zarządzanie zasobami: Wykorzystanie semaforów, mutexów oraz pamięci dzielonej.

## Technologie
- Język: C
- Mechanizmy wielowątkowości: POSIX Threads (pthread)
- IPC: Kolejki komunikatów i pamięć dzielona
- Semafory: POSIX semafory
- Sygnały: Obsługa sygnałów (SIGINT, SIGUSR2)

## Struktura Plików
- `main.c`: Główny plik projektu zawierający logikę symulacji.
- `ticket.h`: Plik nagłówkowy do obsługi biletów.
- `ticket.c`: Plik źródłowy do obsługi biletów.

## Kluczowe Stałe
- `MAX_CHAIRS`: Maksymalna liczba krzesełek.
- `MAX_PEOPLE_ON_CHAIR`: Maksymalna liczba osób na krzesełku.
- `MAX_PEOPLE_ON_PLATFORM`: Maksymalna liczba osób na platformie.
- `NUM_GATES`: Liczba bramek wejściowych.


### Kompilacja
```bash
gcc -o ski_station main.c ticket.c -pthread -lrt
```

### Uruchamianie
```bash
./ski_lift
```

## Przykładowe Raporty
Po zakończeniu symulacji wyświetlane są dane o liczbie przejazdów narciarzy na podstawie zapisów w pamięci dzielonej:
```
[Raport dzienny z pamięci dzielonej]
Narciarz #1 wykonał 3 zjazdy.
Narciarz #2 wykonał 5 zjazdów.
...
```


## Zasoby i Mechanizmy IPC
1. **Pamięć dzielona**: Przechowywanie liczby zjazdów dla każdego narciarza.
2. **Kolejki komunikatów**: Komunikacja między pracownikami stacji.
3. **Semafory**: Zarządzanie dostępem do platformy, kolejki linowej i bramek wejściowych.

## Autor
Jakub Gędłek

