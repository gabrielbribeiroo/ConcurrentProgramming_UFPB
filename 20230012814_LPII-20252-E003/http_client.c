// http_client.c
// Cliente HTTP mínimo usando apenas sockets TCP — sem biblioteca HTTP
#define _POSIX_C_SOURCE 200112L
//
// Uso padrão:  ./http_client
// Uso avançado (desafio extra):
//   ./http_client api.restful-api.dev /api/objects
//   ./http_client httpbin.org /get

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define DEFAULT_HOST "api.restful-api.dev"
#define DEFAULT_PORT "80"
#define DEFAULT_PATH "/api/objects"
#define BUFFER_SIZE  4096

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    const char *path = DEFAULT_PATH;

    // Desafio extra: aceita host e path como argumentos
    if (argc == 3) {
        host = argv[1];
        path = argv[2];
    } else if (argc != 1) {
        fprintf(stderr, "Uso: %s [host path]\n", argv[0]);
        fprintf(stderr, "Exemplo: %s api.restful-api.dev /api/objects\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sock;
    struct addrinfo hints, *res;
    char buffer[BUFFER_SIZE];
    int bytes;

    // Configura parâmetros de resolução DNS
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM;   // TCP

    printf("[HTTP] Resolvendo domínio: %s ...\n", host);

    // Resolve o domínio para IP (equivalente ao nslookup)
    if (getaddrinfo(host, DEFAULT_PORT, &hints, &res) != 0) {
        perror("getaddrinfo falhou");
        exit(EXIT_FAILURE);
    }

    // Cria o socket TCP
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        perror("socket falhou");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    // Conecta ao servidor — realiza o handshake TCP de 3 vias
    printf("[HTTP] Conectando a %s:80 ...\n", host);
    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect falhou — servidor inacessível ou porta fechada");
        close(sock);
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(res);

    printf("[HTTP] Conexão TCP estabelecida.\n\n");

    // Monta a requisição HTTP (texto puro enviado pelo socket)
    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);

    printf("=== Requisição enviada ===\n%s\n", request);

    // Envia a requisição HTTP pelo socket TCP
    if (send(sock, request, strlen(request), 0) < 0) {
        perror("send falhou");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Recebe a resposta em loop até o servidor fechar a conexão
    printf("=== Resposta do servidor ===\n");
    while ((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }

    if (bytes < 0) {
        perror("recv falhou");
    }

    printf("\n=== Fim da resposta ===\n");

    close(sock);
    return 0;
}
