/*
 * sensor_client.c — Cliente IoT com canal TCP de controle e UDP de dados
 *
 * Uso: ./sensor_client <ip_servidor> [porta_tcp] [porta_udp]
 *
 * Arquitetura:
 *   - select() multiplexando stdin, socket TCP e socket UDP
 *   - Estatísticas acumuladas por sensor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <signal.h>
#include <math.h>

#include "protocol.h"

/* =====================================================================
 * Estrutura de estatísticas por sensor
 * ===================================================================== */
typedef struct {
    int    active;
    long   last_seq;       /* último SEQ recebido */
    long   received;       /* total recebido */
    long   lost;           /* pacotes perdidos */
    double min_val;
    double max_val;
    double sum_val;        /* soma para calcular média */
} SensorStats;

static SensorStats stats[MAX_SENSORS];

/* =====================================================================
 * Inicializa estatísticas
 * ===================================================================== */
static void stats_init(void) {
    for (int i = 0; i < MAX_SENSORS; i++) {
        stats[i].active   = 0;
        stats[i].last_seq = 0;
        stats[i].received = 0;
        stats[i].lost     = 0;
        stats[i].min_val  = 1e9;
        stats[i].max_val  = -1e9;
        stats[i].sum_val  = 0.0;
    }
}

/* =====================================================================
 * Atualiza estatísticas com um pacote recebido
 * ===================================================================== */
static void stats_update(int idx, long seq, double value) {
    if (!stats[idx].active) {
        stats[idx].active   = 1;
        stats[idx].last_seq = seq - 1;
    }
    /* Detecta pacotes perdidos */
    long expected = stats[idx].last_seq + 1;
    if (seq > expected)
        stats[idx].lost += seq - expected;
    stats[idx].last_seq = seq;
    stats[idx].received++;
    if (value < stats[idx].min_val) stats[idx].min_val = value;
    if (value > stats[idx].max_val) stats[idx].max_val = value;
    stats[idx].sum_val += value;
}

/* =====================================================================
 * Exibe estatísticas acumuladas
 * ===================================================================== */
static void print_stats(void) {
    printf("\n=== Estatisticas Locais ===\n");
    int any = 0;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!stats[i].active) continue;
        any = 1;
        long total  = stats[i].received + stats[i].lost;
        double pct  = total > 0 ? (stats[i].lost * 100.0 / total) : 0.0;
        double media = stats[i].received > 0
                       ? stats[i].sum_val / stats[i].received : 0.0;
        printf("%-12s: recebidos=%ld, perdidos=%ld (%.1f%%), "
               "min=%.2f, max=%.2f, media=%.2f\n",
               SENSOR_TABLE[i].name,
               stats[i].received,
               stats[i].lost, pct,
               stats[i].min_val, stats[i].max_val, media);
    }
    if (!any) printf("(nenhum sensor ativo ainda)\n");
    printf("===========================\n\n");
}

/* =====================================================================
 * Processa um datagrama UDP recebido
 * ===================================================================== */
static void handle_udp(const char *msg) {
    long  seq;
    char  sensor[MAX_SENSOR_NAME];
    double value;
    char  unit[16];
    long  ts;

    /* SEQ:<n>|SENSOR:<tipo>|VALUE:<val>|UNIT:<u>|TS:<ts> */
    if (sscanf(msg, "SEQ:%ld|SENSOR:%31[^|]|VALUE:%lf|UNIT:%15[^|]|TS:%ld",
               &seq, sensor, &value, unit, &ts) != 5) {
        fprintf(stderr, "[WARN] Datagrama UDP mal formatado: %s\n", msg);
        return;
    }

    int idx = -1;
    for (int i = 0; i < MAX_SENSORS; i++)
        if (strcmp(SENSOR_TABLE[i].name, sensor) == 0) { idx = i; break; }

    if (idx >= 0) stats_update(idx, seq, value);

    printf("[UDP] SEQ:%ld | %-12s | %7.2f %-3s | TS:%ld\n",
           seq, sensor, value, unit, ts);
    fflush(stdout);
}

/* =====================================================================
 * Envia um comando TCP (adiciona \r\n\r\n se necessário)
 * ===================================================================== */
static int send_command(int tcp_fd, const char *cmd) {
    char buf[MAX_BUF];
    /* Verifica se o usuário já incluiu o terminador */
    if (strstr(cmd, "\r\n\r\n"))
        strncpy(buf, cmd, sizeof(buf) - 1);
    else
        snprintf(buf, sizeof(buf), "%s\r\n\r\n", cmd);
    buf[sizeof(buf)-1] = '\0';

    ssize_t n = send(tcp_fd, buf, strlen(buf), MSG_NOSIGNAL);
    return (n > 0) ? 0 : -1;
}

/*
 * Transforma a linha digitada pelo usuário no formato correto do protocolo.
 * Suporta:
 *   START temperatura [porta]
 *   START /sensor/temperatura [UdpPort: porta]
 *   STOP temperatura
 *   STATUS [sensors | temperatura]
 *   stats / quit / EXIT
 */
static int build_command(const char *input, char *out, size_t outsz,
                          int udp_port)
{
    char tmp[512];
    strncpy(tmp, input, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';

    /* Remove \n */
    char *nl = strchr(tmp, '\n');
    if (nl) *nl = '\0';

    char method[64], arg[256];
    method[0] = arg[0] = '\0';
    sscanf(tmp, "%63s %255s", method, arg);

    /* Normaliza para maiúsculas */
    for (char *p = method; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;

    /* stats / quit — tratados fora */
    if (strcmp(method, "STATS") == 0 || strcmp(method, "QUIT") == 0)
        return -2; /* sinal especial */

    if (strcmp(method, "EXIT") == 0) {
        snprintf(out, outsz, "EXIT /\r\n\r\n");
        return 0;
    }

    if (strcmp(method, "START") == 0) {
        /* arg pode ser "temperatura" ou "/sensor/temperatura" */
        const char *sname = (arg[0] == '/') ? (strrchr(arg, '/') + 1) : arg;
        snprintf(out, outsz,
            "START /sensor/%s\r\nUdpPort: %d\r\n\r\n",
            sname, udp_port);
        return 0;
    }

    if (strcmp(method, "STOP") == 0) {
        const char *sname = (arg[0] == '/') ? (strrchr(arg, '/') + 1) : arg;
        snprintf(out, outsz, "STOP /sensor/%s\r\n\r\n", sname);
        return 0;
    }

    if (strcmp(method, "STATUS") == 0) {
        if (arg[0] == '\0' || strcmp(arg, "sensors") == 0 ||
            strcmp(arg, "/sensors") == 0) {
            snprintf(out, outsz, "STATUS /sensors\r\n\r\n");
        } else {
            const char *sname = (arg[0] == '/') ? (strrchr(arg, '/') + 1) : arg;
            snprintf(out, outsz, "STATUS /sensor/%s\r\n\r\n", sname);
        }
        return 0;
    }

    /* Encaminha literal (usuário já formatou) */
    snprintf(out, outsz, "%s\r\n\r\n", tmp);
    return 0;
}

/* =====================================================================
 * main
 * ===================================================================== */
int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <ip_servidor> [porta_tcp] [porta_udp]\n",
                argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int tcp_port = (argc > 2) ? atoi(argv[2]) : DEFAULT_TCP_PORT;
    int udp_port = (argc > 3) ? atoi(argv[3]) : DEFAULT_UDP_PORT;

    stats_init();

    /* ----- Socket TCP ----- */
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) { perror("socket TCP"); return 1; }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(tcp_port);
    if (inet_pton(AF_INET, server_ip, &srv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Endereço IP inválido: %s\n", server_ip);
        return 1;
    }

    if (connect(tcp_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("connect"); return 1;
    }
    printf("Conectado ao servidor %s:%d\n", server_ip, tcp_port);

    /* ----- Socket UDP (recebe dados) ----- */
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("socket UDP"); return 1; }

    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family      = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port        = htons(udp_port);

    if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("bind UDP"); return 1;
    }
    printf("Socket UDP escutando na porta %d\n", udp_port);
    printf("Digite comandos (START, STOP, STATUS, stats, quit):\n\n");

    int maxfd = (tcp_fd > udp_fd ? tcp_fd : udp_fd) + 1;
    if (STDIN_FILENO + 1 > maxfd) maxfd = STDIN_FILENO + 1;

    /* Buffer acumulador para respostas TCP */
    char tcp_accum[MAX_BUF * 4];
    int  tcp_accum_len = 0;
    memset(tcp_accum, 0, sizeof(tcp_accum));

    printf("> ");
    fflush(stdout);

    int running = 1;
    while (running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(tcp_fd,       &rfds);
        FD_SET(udp_fd,       &rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv = {0, 100000}; /* 100 ms */
        int ret = select(maxfd, &rfds, NULL, NULL, &tv);
        if (ret < 0) { if (errno == EINTR) continue; break; }

        /* ----- stdin: comando do usuário ----- */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char line[512];
            if (!fgets(line, sizeof(line), stdin)) break;

            /* Verifica comandos locais */
            char word[64];
            sscanf(line, "%63s", word);
            for (char *p = word; *p; p++)
                if (*p >= 'a' && *p <= 'z') *p -= 32;

            if (strcmp(word, "STATS") == 0) {
                print_stats();
                printf("> "); fflush(stdout);
                continue;
            }
            if (strcmp(word, "QUIT") == 0) {
                printf("Enviando EXIT ao servidor...\n");
                send_command(tcp_fd, "EXIT /\r\n\r\n");
                running = 0;
                continue;
            }

            char cmd[MAX_BUF];
            int r = build_command(line, cmd, sizeof(cmd), udp_port);
            if (r == -2) {
                /* stats/quit já tratados acima */
                printf("> "); fflush(stdout);
                continue;
            }
            if (send_command(tcp_fd, cmd) < 0) {
                fprintf(stderr, "[ERRO] Falha ao enviar comando TCP.\n");
                running = 0;
            }
        }

        /* ----- TCP: resposta do servidor ----- */
        if (FD_ISSET(tcp_fd, &rfds)) {
            char buf[MAX_BUF];
            ssize_t n = recv(tcp_fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) { printf("\n[INFO] Servidor desconectado.\n"); running = 0; continue; }
            buf[n] = '\0';

            if (tcp_accum_len + n < (int)sizeof(tcp_accum) - 1) {
                memcpy(tcp_accum + tcp_accum_len, buf, n);
                tcp_accum_len += n;
                tcp_accum[tcp_accum_len] = '\0';
            }

            char *end;
            while ((end = strstr(tcp_accum, "\r\n\r\n")) != NULL) {
                *end = '\0';
                printf("\nResposta: %s\n", tcp_accum);
                fflush(stdout);

                end += 4;
                tcp_accum_len -= (int)(end - tcp_accum);
                if (tcp_accum_len > 0)
                    memmove(tcp_accum, end, tcp_accum_len);
                else
                    tcp_accum_len = 0;
                tcp_accum[tcp_accum_len] = '\0';
            }
            printf("> "); fflush(stdout);
        }

        /* ----- UDP: dado de sensor ----- */
        if (FD_ISSET(udp_fd, &rfds)) {
            char buf[MAX_MSG_LEN];
            ssize_t n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0, NULL, NULL);
            if (n > 0) {
                buf[n] = '\0';
                handle_udp(buf);
                /* Re-exibe prompt após dado UDP */
                printf("> "); fflush(stdout);
            }
        }
    }

    close(tcp_fd);
    close(udp_fd);
    printf("Conexão encerrada.\n");
    return 0;
}
