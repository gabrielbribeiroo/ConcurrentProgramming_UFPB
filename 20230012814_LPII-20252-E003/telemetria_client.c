// telemetria_client.c
// Drone — envia dados de telemetria via UDP para a central de controle
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define SERVER_IP   "127.0.0.1"
#define PORT        9000
#define NUM_PACKETS 10
#define BUFFER_SIZE 256

int main() {
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in servaddr;

    // Cria socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket falhou");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &servaddr.sin_addr) <= 0) {
        perror("inet_pton falhou");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Semente para valores aleatórios de velocidade
    srand((unsigned int)time(NULL));

    double lat = -7.2100;
    double lon = -39.3150;

    printf("[Drone] Iniciando envio de telemetria para %s:%d\n\n", SERVER_IP, PORT);

    for (int pkt = 1; pkt <= NUM_PACKETS; pkt++) {
        // Velocidade aleatória entre 10.0 e 50.0
        double vel = 10.0 + ((double)rand() / RAND_MAX) * 40.0;
        long   ts  = (long)time(NULL);

        // Formata o datagrama
        snprintf(buffer, sizeof(buffer),
                 "PKT:%d|LAT:%.4f|LON:%.4f|VEL:%.1f|TS:%ld",
                 pkt, lat, lon, vel, ts);

        ssize_t sent = sendto(sockfd, buffer, strlen(buffer), 0,
                              (const struct sockaddr *)&servaddr,
                              sizeof(servaddr));
        if (sent < 0) {
            perror("sendto falhou");
        } else {
            printf("[Drone] Pacote %d enviado: %s\n", pkt, buffer);
        }

        // Incrementa posição para próximo envio
        lat += 0.0001;
        lon += 0.0001;

        sleep(1);
    }

    printf("\n[Drone] Transmissão encerrada. %d pacotes enviados.\n", NUM_PACKETS);
    close(sockfd);
    return 0;
}
