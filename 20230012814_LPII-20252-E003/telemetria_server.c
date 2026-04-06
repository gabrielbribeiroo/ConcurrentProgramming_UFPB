// telemetria_server.c
// Central de controle — recebe datagramas UDP do drone
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 9000
#define BUFFER_SIZE 256

static volatile int running = 1;

void handle_sigint(int sig) {
    (void)sig;
    printf("\n[Servidor] Encerrando...\n");
    running = 0;
}

int main() {
    int sockfd;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len;
    int pkt_count = 0;

    signal(SIGINT, handle_sigint);

    // Cria socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket falhou");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port        = htons(PORT);

    // Vincula à porta
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind falhou");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[Servidor] Central de controle escutando na porta %d...\n\n", PORT);
    printf("%-6s %-12s %-12s %-10s %-14s\n",
           "PKT", "LATITUDE", "LONGITUDE", "VEL(km/h)", "TIMESTAMP");
    printf("----------------------------------------------------------\n");

    len = sizeof(cliaddr);

    while (running) {
        int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&cliaddr, &len);
        if (n < 0) {
            if (!running) break;
            perror("recvfrom falhou");
            continue;
        }
        buffer[n] = '\0';

        // Parseia a string "PKT:N|LAT:X|LON:Y|VEL:Z|TS:T"
        int pkt;
        double lat, lon, vel;
        long ts;
        if (sscanf(buffer, "PKT:%d|LAT:%lf|LON:%lf|VEL:%lf|TS:%ld",
                   &pkt, &lat, &lon, &vel, &ts) == 5) {
            pkt_count++;
            printf("%-6d %-12.4f %-12.4f %-10.1f %-14ld\n",
                   pkt, lat, lon, vel, ts);
            fflush(stdout);
        } else {
            printf("[Servidor] Datagrama malformado: %s\n", buffer);
        }
    }

    printf("\n[Servidor] Total de pacotes recebidos: %d\n", pkt_count);
    close(sockfd);
    return 0;
}
