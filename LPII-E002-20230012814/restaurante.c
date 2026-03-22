#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>

/* Parâmetros calculados para matrícula 20230012814 (M = 2814)
   NUM_COZINHEIROS = (2814 % 4) + 2 = 4
   NUM_FOGOES = (2814 % 3) + 2 = 2
   NUM_MESAS = ((2814 / 10) % 5) + 4 = 5
   NUM_GARCONS = ((2814 / 100) % 3) + 2 = 3
   TEMPO_PREPARO = ((2814 / 100) % 3) + 1 = 2 segundos
*/

#define NUM_COZINHEIROS 4
#define NUM_FOGOES 2
#define NUM_MESAS 5
#define NUM_GARCONS 3
#define TEMPO_PREPARO 2
#define MAX_FILA_PEDIDOS 10

// Estrutura de Pedido
typedef struct {
    int id;
    int mesa_id;
    int prato;
    int status; // 0: pendente, 1: em preparo, 2: pronto
} Pedido;

// Estrutura para estatísticas
typedef struct {
    int pedidos_totais;
    int pedidos_atendidos;
    double tempo_inicio;
    double tempo_total_espera;
    pthread_mutex_t mutex;
} Estatisticas;

// Variáveis Globais
Estatisticas stats;
Pedido fila_pedidos[MAX_FILA_PEDIDOS];
int head = 0, tail = 0;
int pedidos_na_fila = 0;
int running = 1;

// Mecanismos de Sincronização
pthread_mutex_t mutex_fila;
sem_t sem_vagas_fila;
sem_t sem_pedidos_fila;
sem_t sem_fogoes;
pthread_barrier_t barreira_atendimento;

// Protótipos
void* thread_mesa(void* arg);
void* thread_garcom(void* arg);
void* thread_cozinheiro(void* arg);

// Variáveis para sincronizar mesa e garçom
sem_t sem_atendimento_mesa[NUM_MESAS]; // Mesa espera garçom
sem_t sem_pedido_pronto[NUM_MESAS];   // Mesa espera comida

void* thread_mesa(void* arg) {
    int id = *(int*)arg;
    free(arg);

    printf("[MESA %d] Chegou ao restaurante.\n", id);

    pthread_barrier_wait(&barreira_atendimento);

    printf("[MESA %d] Fazendo pedido...\n", id);
    sem_post(&sem_atendimento_mesa[id]); 

    sem_wait(&sem_pedido_pronto[id]);
    printf("[MESA %d] REFEIÇÃO RECEBIDA! Degustando...\n", id);
    sleep(1); 
    printf("[MESA %d] Satisfeita, saindo do restaurante.\n", id);

    return NULL;
}

void* thread_garcom(void* arg) {
    int id = *(int*)arg;
    free(arg);

    while (1) {
        for(int i = 0; i < NUM_MESAS; i++) {
            if (sem_trywait(&sem_atendimento_mesa[i]) == 0) {
                printf("  [GARÇOM %d] Atendendo Mesa %d.\n", id, i);
                
                sem_wait(&sem_vagas_fila);
                pthread_mutex_lock(&mutex_fila);
                
                Pedido p;
                p.id = ++stats.pedidos_totais;
                p.mesa_id = i;
                p.prato = rand() % 10;
                p.status = 0;
                
                fila_pedidos[tail] = p;
                tail = (tail + 1) % MAX_FILA_PEDIDOS;
                pedidos_na_fila++;
                
                printf("  [GARÇOM %d] Pedido %d (Mesa %d) enviado para cozinha.\n", id, p.id, i);
                
                pthread_mutex_unlock(&mutex_fila);
                sem_post(&sem_pedidos_fila);
                break; 
            }
        }
        
        pthread_mutex_lock(&stats.mutex);
        if (stats.pedidos_atendidos >= NUM_MESAS) {
            pthread_mutex_unlock(&stats.mutex);
            break;
        }
        pthread_mutex_unlock(&stats.mutex);
        
        if (!running) break;

        usleep(100000); 
    }
    printf("  [GARÇOM %d] Finalizou turno.\n", id);
    return NULL;
}

void* thread_cozinheiro(void* arg) {
    int id = *(int*)arg;
    free(arg);

    while (1) {
        // Consumir pedido da fila
        if (!running && pedidos_na_fila == 0) break;

        // Se usar sem_wait aqui, pode travar. Usaremos sem_trywait ou aviso de término
        if (sem_trywait(&sem_pedidos_fila) != 0) {
            if (!running) break;
            usleep(100000);
            continue;
        }
        
        pthread_mutex_lock(&mutex_fila);
        if (pedidos_na_fila == 0) { // Safety check
            pthread_mutex_unlock(&mutex_fila);
            continue;
        }
        
        Pedido p = fila_pedidos[head];
        head = (head + 1) % MAX_FILA_PEDIDOS;
        pedidos_na_fila--;
        pthread_mutex_unlock(&mutex_fila);
        sem_post(&sem_vagas_fila);

        printf("Cozinheiro [%d] pegou pedido %d (Mesa %d).\n", id, p.id, p.mesa_id);

        // Acessar fogão (recurso limitado)
        sem_wait(&sem_fogoes);
        printf("Cozinheiro [%d] usando fogão para o pedido %d.\n", id, p.id);
        
        // Simular tempo de preparo
        sleep(TEMPO_PREPARO);
        
        printf("Cozinheiro [%d] finalizou pedido %d e liberou o fogão.\n", id, p.id);
        sem_post(&sem_fogoes);

        // Notificar mesa que o pedido está pronto
        sem_post(&sem_pedido_pronto[p.mesa_id]);

        // Atualizar estatísticas
        pthread_mutex_lock(&stats.mutex);
        stats.pedidos_atendidos++;
        // Estimativa simples de espera para estatísticas
        stats.tempo_total_espera += TEMPO_PREPARO + 1.0; 
        
        if (stats.pedidos_atendidos >= NUM_MESAS) {
            pthread_mutex_unlock(&stats.mutex);
            break;
        }
        pthread_mutex_unlock(&stats.mutex);
    }

    printf("Cozinheiro [%d] finalizou seu turno.\n", id);
    return NULL;
}

int main() {
    srand(time(NULL));
    printf("--- Simulador de Restaurante Concorrente ---\n");
    printf("Parâmetros: Cozinheiros: %d, Fogões: %d, Mesas: %d, Garçons: %d\n", 
            NUM_COZINHEIROS, NUM_FOGOES, NUM_MESAS, NUM_GARCONS);

    // Inicialização
    pthread_mutex_init(&stats.mutex, NULL);
    stats.pedidos_totais = 0;
    stats.pedidos_atendidos = 0;
    stats.tempo_total_espera = 0;
    stats.tempo_inicio = (double)time(NULL);

    pthread_mutex_init(&mutex_fila, NULL);
    sem_init(&sem_vagas_fila, 0, MAX_FILA_PEDIDOS);
    sem_init(&sem_pedidos_fila, 0, 0);
    sem_init(&sem_fogoes, 0, NUM_FOGOES);
    pthread_barrier_init(&barreira_atendimento, NULL, NUM_MESAS);

    for (int i = 0; i < NUM_MESAS; i++) {
        sem_init(&sem_atendimento_mesa[i], 0, 0);
        sem_init(&sem_pedido_pronto[i], 0, 0);
    }

    pthread_t t_mesas[NUM_MESAS];
    pthread_t t_garcons[NUM_GARCONS];
    pthread_t t_cozinheiros[NUM_COZINHEIROS];

    // Criar cozinheiros
    for (int i = 0; i < NUM_COZINHEIROS; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&t_cozinheiros[i], NULL, thread_cozinheiro, id);
    }

    // Criar garçons
    for (int i = 0; i < NUM_GARCONS; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&t_garcons[i], NULL, thread_garcom, id);
    }

    // Criar mesas
    for (int i = 0; i < NUM_MESAS; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&t_mesas[i], NULL, thread_mesa, id);
    }

    // Join
    for (int i = 0; i < NUM_MESAS; i++) pthread_join(t_mesas[i], NULL);
    
    // Sinalizar fim para garçons e cozinheiros
    pthread_mutex_lock(&stats.mutex);
    running = 0;
    pthread_mutex_unlock(&stats.mutex);

    for (int i = 0; i < NUM_GARCONS; i++) pthread_join(t_garcons[i], NULL);
    for (int i = 0; i < NUM_COZINHEIROS; i++) pthread_join(t_cozinheiros[i], NULL);

    printf("\n--- Relatório Final ---\n");
    double tempo_final = (double)time(NULL);
    double duracao = tempo_final - stats.tempo_inicio;
    if (duracao == 0) duracao = 1; // Evitar divisão por zero

    printf("Pedidos Totais Atendidos: %d\n", stats.pedidos_atendidos);
    printf("Tempo Total de Execução: %.0f s\n", duracao);
    printf("Operações por Segundo (OPS): %.2f\n", (double)stats.pedidos_atendidos / duracao);
    printf("Tempo Médio de Espera Estimado: %.2f s\n", stats.tempo_total_espera / stats.pedidos_atendidos);
    printf("Simulação finalizada com sucesso.\n");

    //Cleanup
    pthread_mutex_destroy(&stats.mutex);
    pthread_mutex_destroy(&mutex_fila);
    sem_destroy(&sem_vagas_fila);
    sem_destroy(&sem_pedidos_fila);
    sem_destroy(&sem_fogoes);
    for (int i = 0; i < NUM_MESAS; i++) {
        sem_destroy(&sem_atendimento_mesa[i]);
        sem_destroy(&sem_pedido_pronto[i]);
    }
    pthread_barrier_destroy(&barreira_atendimento);

    return 0;
}
