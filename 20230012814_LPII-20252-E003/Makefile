# Makefile — Exercício Prático 3: Sockets UDP, HTTP sobre TCP
CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11

all: telemetria_server telemetria_client http_client

telemetria_server: telemetria_server.c
	$(CC) $(CFLAGS) -o telemetria_server telemetria_server.c

telemetria_client: telemetria_client.c
	$(CC) $(CFLAGS) -o telemetria_client telemetria_client.c

http_client: http_client.c
	$(CC) $(CFLAGS) -o http_client http_client.c

clean:
	rm -f telemetria_server telemetria_client http_client

.PHONY: all clean
