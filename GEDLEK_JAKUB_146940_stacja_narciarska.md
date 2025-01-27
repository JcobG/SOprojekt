Opis
Projekt zostanie zaimplementowany w języku C i prawdopodobnie składać się będzie z następujących modułów:
Obsługa zakupu biletów
Obsługa kolejki linowej: Kontrolowanie liczby narciarzy na platformie oraz w kolejce
Obsługa bramek: Wpuszczanie narciarzy na platformę.
Obsługa narciarzy: Symulacja korzystania z tras narciarskich oraz różnych typów biletów (VIP, normalny, dziecięcy).
Obsługa pracowników i ich komunikacji
Obsługa symulacji stacji
Komunikacja międzyprocesowa: Wykorzystano kolejki komunikatów, pamięć dzieloną oraz semafory do synchronizacji
Obsługa sygnałów: Dodano możliwość zatrzymania programu za pomocą sygnału SIGINT.

Testy
Test obsługi VIP: Sprawdzono, czy narciarze VIP mają pierwszeństwo na krzesłach. 
Test dzieci i opiekunów: Sprawdzono, czy dzieci bez opiekunów nie mogą korzystać z kolejki, albo gdy opiekunowie mają max. liczbę dzieci
Test zniżki: czy działa dla poniżej 12 a powyżej 65 r.ż
Test obciążenia: Symulacja maksymalnej liczby narciarzy na platformie i w kolejce.
