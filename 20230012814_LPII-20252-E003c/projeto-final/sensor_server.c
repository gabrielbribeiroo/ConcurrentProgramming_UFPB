/*
 * sensor_server.c — Servidor IoT com canal TCP de controle e UDP de dados
 *
 * Uso: ./sensor_server [porta_tcp]
 *
 * Arquitetura:
 *   - Thread principal: aceita conexão TCP e processa comandos via select()
 *   - Uma pthread por sensor ativo: gera e envia datagramas UDP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <signal.h>

#include "protocol.h"

/* =====================================================================
 * Estrutura de estado de cada sensor
 * ===================================================================== */
typedef struct {
    int            active;           /* 1 = transmitindo */
    int            udp_sock;         /* socket UDP para envio */
    struct sockaddr_in client_addr;  /* destino UDP */
    pthread_t      thread;           /* thread de streaming */
    pthread_mutex_t lock;
    int            stop_flag;        /* sinaliza parada da thread */
    long           seq;              /* número de sequência atual */
    long           total_sent;       /* total de pacotes enviados */
    double         current_value;    /* valor atual (random walk) */
    int            sensor_idx;       /* índice em SENSOR_TABLE */
} SensorState;

/* =====================================================================
 * Globais
 * ===================================================================== */
static SensorState sensors[MAX_SENSORS];
static int         g_running = 1;

/* =====================================================================
 * Utilitários
 * ===================================================================== */

/* Retorna o índice do sensor pelo nome, ou -1 */
static int find_sensor(const char *name) {
    for (int i = 0; i < MAX_SENSORS; i++)
        if (strcmp(SENSOR_TABLE[i].name, name) == 0) return i;
    return -1;
}

/* Remove \r\n do final de uma string */
static void strip_crlf(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' '))
        s[--len] = '\0';
}

/* Envia resposta TCP garantindo entrega completa */
static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/* Monta e envia uma resposta */
static void send_response(int tcp_fd, const char *body) {
    send_all(tcp_fd, body, strlen(body));
}

/* =====================================================================
 * Thread de streaming UDP para um sensor
 * ===================================================================== */
static void *stream_thread(void *arg) {
    SensorState *s = (SensorState *)arg;
    int idx = s->sensor_idx;
    const SensorInfo *info = &SENSOR_TABLE[idx];
    char buf[MAX_MSG_LEN];

    srand((unsigned)(time(NULL) ^ (uintptr_t)s));

    /* Inicializa valor no meio da faixa */
    s->current_value = (info->min_val + info->max_val) / 2.0;

    while (1) {
        pthread_mutex_lock(&s->lock);
        int stop = s->stop_flag;
        pthread_mutex_unlock(&s->lock);
        if (stop) break;

        /* Random walk limitado à faixa */
        double delta = ((rand() % 201) - 100) / 200.0;  /* -0.5 a +0.5 */
        s->current_value += delta;
        if (s->current_value < info->min_val) s->current_value = info->min_val;
        if (s->current_value > info->max_val) s->current_value = info->max_val;

        pthread_mutex_lock(&s->lock);
        s->seq++;
        long seq = s->seq;
        pthread_mutex_unlock(&s->lock);

        time_t ts = time(NULL);
        int len = snprintf(buf, sizeof(buf),
            "SEQ:%ld|SENSOR:%s|VALUE:%.2f|UNIT:%s|TS:%ld",
            seq, info->name, s->current_value, info->unit, (long)ts);

        sendto(s->udp_sock, buf, len, 0,
               (struct sockaddr *)&s->client_addr,
               sizeof(s->client_addr));

        pthread_mutex_lock(&s->lock);
        s->total_sent++;
        pthread_mutex_unlock(&s->lock);

        usleep(info->interval_us);
    }
    return NULL;
}

/* =====================================================================
 * Processamento de comandos TCP
 * ===================================================================== */

/*
 * Parseia a requisição recebida e preenche:
 *   method   — "START" / "STOP" / "STATUS" / "EXIT"
 *   resource — "/sensor/temperatura" etc.
 *   udp_port — porta extraída do header UdpPort (se presente)
 */
static void parse_request(const char *raw,
                           char *method, size_t msz __attribute__((unused)),
                           char *resource, size_t rsz __attribute__((unused)),
                           int *udp_port)
{
    *udp_port = -1;
    method[0] = resource[0] = '\0';

    /* Trabalha em cópia para não modificar o buffer original */
    char tmp[MAX_BUF];
    strncpy(tmp, raw, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';

    char *line = strtok(tmp, "\n");
    int first = 1;
    while (line) {
        strip_crlf(line);
        if (first) {
            /* Primeira linha: MÉTODO /recurso */
            sscanf(line, "%63s %255s", method, resource);
            first = 0;
        } else {
            /* Headers subsequentes */
            if (strncasecmp(line, "UdpPort:", 8) == 0) {
                *udp_port = atoi(line + 8);
            }
        }
        line = strtok(NULL, "\n");
    }
}

/* Extrai o nome do sensor de um resource como "/sensor/temperatura" */
static int extract_sensor_name(const char *resource, char *name, size_t nsz) {
    /* Aceita /sensor/<nome> */
    const char *prefix = "/sensor/";
    if (strncmp(resource, prefix, strlen(prefix)) != 0) return -1;
    strncpy(name, resource + strlen(prefix), nsz - 1);
    name[nsz-1] = '\0';
    strip_crlf(name);
    return 0;
}

/* Processa um comando completo e envia a resposta ao cliente */
static void handle_command(int tcp_fd,
                            const char *method,
                            const char *resource,
                            int udp_port,
                            const char *client_ip)
{
    char resp[MAX_BUF];

    /* ----- EXIT ----- */
    if (strcmp(method, "EXIT") == 0) {
        snprintf(resp, sizeof(resp),
            "200 OK\r\nMessage: bye\r\n\r\n");
        send_response(tcp_fd, resp);
        printf("[LOG] Comando: EXIT /\n");
        return;
    }

    /* ----- STATUS /sensors ----- */
    if (strcmp(method, "STATUS") == 0 && strcmp(resource, "/sensors") == 0) {
        printf("[LOG] Comando: STATUS /sensors\n");
        char body[MAX_BUF];
        int pos = 0;
        for (int i = 0; i < MAX_SENSORS; i++) {
            pthread_mutex_lock(&sensors[i].lock);
            if (sensors[i].active)
                pos += snprintf(body + pos, sizeof(body) - pos,
                    "%s: streaming (seq=%ld)\r\n",
                    SENSOR_TABLE[i].name, sensors[i].seq);
            else
                pos += snprintf(body + pos, sizeof(body) - pos,
                    "%s: inactive\r\n", SENSOR_TABLE[i].name);
            pthread_mutex_unlock(&sensors[i].lock);
        }
        snprintf(resp, sizeof(resp),
            "200 OK\r\nCount: %d\r\n\r\n", MAX_SENSORS);
        send_response(tcp_fd, resp);
        send_response(tcp_fd, body);
        return;
    }

    /* ----- STATUS /sensor/<tipo> ----- */
    if (strcmp(method, "STATUS") == 0 &&
        strncmp(resource, "/sensor/", 8) == 0)
    {
        char sname[MAX_SENSOR_NAME];
        extract_sensor_name(resource, sname, sizeof(sname));
        int idx = find_sensor(sname);
        printf("[LOG] Comando: STATUS /sensor/%s\n", sname);
        if (idx < 0) {
            snprintf(resp, sizeof(resp),
                "404 Not Found\r\nError: sensor '%s' nao existe\r\n\r\n", sname);
            send_response(tcp_fd, resp);
            return;
        }
        pthread_mutex_lock(&sensors[idx].lock);
        int act  = sensors[idx].active;
        long seq = sensors[idx].seq;
        long tot = sensors[idx].total_sent;
        pthread_mutex_unlock(&sensors[idx].lock);

        snprintf(resp, sizeof(resp),
            "200 OK\r\nSensor: %s\r\nStatus: %s\r\nSeq: %ld\r\nTotalSent: %ld\r\n\r\n",
            sname, act ? "streaming" : "inactive", seq, tot);
        send_response(tcp_fd, resp);
        return;
    }

    /* ----- START /sensor/<tipo> ----- */
    if (strcmp(method, "START") == 0) {
        char sname[MAX_SENSOR_NAME];
        if (extract_sensor_name(resource, sname, sizeof(sname)) < 0) {
            snprintf(resp, sizeof(resp),
                "400 Bad Request\r\nError: recurso invalido\r\n\r\n");
            send_response(tcp_fd, resp);
            return;
        }
        int idx = find_sensor(sname);
        if (idx < 0) {
            snprintf(resp, sizeof(resp),
                "404 Not Found\r\nError: sensor '%s' nao existe\r\n\r\n", sname);
            send_response(tcp_fd, resp);
            return;
        }
        if (udp_port <= 0) {
            snprintf(resp, sizeof(resp),
                "400 Bad Request\r\nError: UdpPort ausente ou invalido\r\n\r\n");
            send_response(tcp_fd, resp);
            return;
        }
        pthread_mutex_lock(&sensors[idx].lock);
        if (sensors[idx].active) {
            pthread_mutex_unlock(&sensors[idx].lock);
            snprintf(resp, sizeof(resp),
                "409 Conflict\r\nError: sensor '%s' ja esta ativo\r\n\r\n", sname);
            send_response(tcp_fd, resp);
            return;
        }

        /* Cria socket UDP de envio */
        int usock = socket(AF_INET, SOCK_DGRAM, 0);
        if (usock < 0) {
            pthread_mutex_unlock(&sensors[idx].lock);
            snprintf(resp, sizeof(resp),
                "500 Internal Error\r\nError: nao foi possivel criar socket UDP\r\n\r\n");
            send_response(tcp_fd, resp);
            return;
        }

        memset(&sensors[idx].client_addr, 0, sizeof(sensors[idx].client_addr));
        sensors[idx].client_addr.sin_family = AF_INET;
        sensors[idx].client_addr.sin_port   = htons(udp_port);
        inet_pton(AF_INET, client_ip, &sensors[idx].client_addr.sin_addr);

        sensors[idx].udp_sock  = usock;
        sensors[idx].active    = 1;
        sensors[idx].stop_flag = 0;
        sensors[idx].seq       = 0;
        sensors[idx].total_sent = 0;
        pthread_mutex_unlock(&sensors[idx].lock);

        pthread_create(&sensors[idx].thread, NULL, stream_thread, &sensors[idx]);

        printf("[LOG] Comando: START /sensor/%s (UDP -> %s:%d)\n",
               sname, client_ip, udp_port);
        printf("[LOG] Fluxo %s INICIADO (intervalo: %dms)\n",
               sname, SENSOR_TABLE[idx].interval_us / 1000);

        snprintf(resp, sizeof(resp),
            "200 OK\r\nSensor: %s\r\nStatus: streaming\r\n"
            "UdpTarget: %s:%d\r\nInterval: %dms\r\n\r\n",
            sname, client_ip, udp_port,
            SENSOR_TABLE[idx].interval_us / 1000);
        send_response(tcp_fd, resp);
        return;
    }

    /* ----- STOP /sensor/<tipo> ----- */
    if (strcmp(method, "STOP") == 0) {
        char sname[MAX_SENSOR_NAME];
        if (extract_sensor_name(resource, sname, sizeof(sname)) < 0) {
            snprintf(resp, sizeof(resp),
                "400 Bad Request\r\nError: recurso invalido\r\n\r\n");
            send_response(tcp_fd, resp);
            return;
        }
        int idx = find_sensor(sname);
        if (idx < 0) {
            snprintf(resp, sizeof(resp),
                "404 Not Found\r\nError: sensor '%s' nao existe\r\n\r\n", sname);
            send_response(tcp_fd, resp);
            return;
        }
        pthread_mutex_lock(&sensors[idx].lock);
        if (!sensors[idx].active) {
            pthread_mutex_unlock(&sensors[idx].lock);
            snprintf(resp, sizeof(resp),
                "409 Conflict\r\nError: sensor '%s' ja esta inativo\r\n\r\n", sname);
            send_response(tcp_fd, resp);
            return;
        }
        sensors[idx].stop_flag = 1;
        long total = sensors[idx].total_sent;
        pthread_mutex_unlock(&sensors[idx].lock);

        pthread_join(sensors[idx].thread, NULL);

        pthread_mutex_lock(&sensors[idx].lock);
        sensors[idx].active = 0;
        close(sensors[idx].udp_sock);
        pthread_mutex_unlock(&sensors[idx].lock);

        printf("[LOG] Comando: STOP /sensor/%s\n", sname);
        printf("[LOG] Fluxo %s PARADO (total enviados: %ld pacotes)\n",
               sname, total);

        snprintf(resp, sizeof(resp),
            "200 OK\r\nSensor: %s\r\nStatus: stopped\r\nTotalSent: %ld\r\n\r\n",
            sname, total);
        send_response(tcp_fd, resp);
        return;
    }

    /* ----- Comando desconhecido ----- */
    snprintf(resp, sizeof(resp),
        "400 Bad Request\r\nError: metodo '%s' desconhecido\r\n\r\n", method);
    send_response(tcp_fd, resp);
}

/* =====================================================================
 * Loop principal de leitura TCP com select()
 * ===================================================================== */
static void client_loop(int tcp_fd, const char *client_ip) {
    char buf[MAX_BUF];
    char accum[MAX_BUF * 4];
    int  accum_len = 0;

    memset(accum, 0, sizeof(accum));

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(tcp_fd, &rfds);

        struct timeval tv = {1, 0};
        int ret = select(tcp_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret == 0) continue;

        ssize_t n = recv(tcp_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        /* Acumula no buffer */
        if (accum_len + n < (int)sizeof(accum) - 1) {
            memcpy(accum + accum_len, buf, n);
            accum_len += n;
            accum[accum_len] = '\0';
        }

        /* Processa mensagens completas (terminadas em \r\n\r\n) */
        char *end;
        while ((end = strstr(accum, "\r\n\r\n")) != NULL) {
            *end = '\0';
            char method[64], resource[256];
            int  udp_port;
            parse_request(accum, method, sizeof(method),
                          resource, sizeof(resource), &udp_port);

            int is_exit = (strcmp(method, "EXIT") == 0);
            handle_command(tcp_fd, method, resource, udp_port, client_ip);

            /* Move restante do buffer */
            end += 4; /* pula \r\n\r\n */
            accum_len -= (int)(end - accum);
            if (accum_len > 0)
                memmove(accum, end, accum_len);
            else
                accum_len = 0;
            accum[accum_len] = '\0';

            if (is_exit) { g_running = 0; return; }
        }
    }
}

/* =====================================================================
 * main
 * ===================================================================== */
int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    int tcp_port = (argc > 1) ? atoi(argv[1]) : DEFAULT_TCP_PORT;

    /* Inicializa estruturas de sensores */
    for (int i = 0; i < MAX_SENSORS; i++) {
        memset(&sensors[i], 0, sizeof(sensors[i]));
        sensors[i].sensor_idx = i;
        pthread_mutex_init(&sensors[i].lock, NULL);
    }

    /* Cria socket TCP */
    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(tcp_port);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv_fd, 5) < 0) { perror("listen"); return 1; }

    printf("[LOG] Servidor TCP escutando na porta %d...\n", tcp_port);

    /* Loop de aceitação (suporte a reconexão após EXIT) */
    while (g_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_fd = accept(srv_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) { if (errno == EINTR) continue; break; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));
        printf("[LOG] Cliente conectado: %s\n", ip);

        g_running = 1; /* reset para nova sessão */
        client_loop(cli_fd, ip);
        close(cli_fd);

        /* Para todos os sensores ainda ativos */
        for (int i = 0; i < MAX_SENSORS; i++) {
            pthread_mutex_lock(&sensors[i].lock);
            if (sensors[i].active) {
                sensors[i].stop_flag = 1;
                pthread_mutex_unlock(&sensors[i].lock);
                pthread_join(sensors[i].thread, NULL);
                pthread_mutex_lock(&sensors[i].lock);
                sensors[i].active = 0;
                close(sensors[i].udp_sock);
            }
            pthread_mutex_unlock(&sensors[i].lock);
        }

        printf("[LOG] Cliente desconectado.\n");
        g_running = 1; /* pronto para próxima conexão */
    }

    close(srv_fd);
    for (int i = 0; i < MAX_SENSORS; i++)
        pthread_mutex_destroy(&sensors[i].lock);
    return 0;
}
