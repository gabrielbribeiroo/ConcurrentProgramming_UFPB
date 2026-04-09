# Makefile — Sistema de Monitoramento IoT (TCP + UDP)

CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -O2
LDFLAGS = -pthread -lm

.PHONY: all server client clean

all: server client

server: sensor_server.c protocol.h
	$(CC) $(CFLAGS) -o sensor_server sensor_server.c $(LDFLAGS)

client: sensor_client.c protocol.h
	$(CC) $(CFLAGS) -o sensor_client sensor_client.c $(LDFLAGS)

clean:
	rm -f sensor_server sensor_client *.o
