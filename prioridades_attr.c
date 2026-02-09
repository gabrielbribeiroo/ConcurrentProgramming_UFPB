#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define PILHA_MAIOR (1024*1024) // 1 MB
#define PILHA_MENOR (64*1024) // 64 KB
#define NOT_DETACHED 0
#define DETACHED 1

pthread_attr_t criar_atributos(size_t tamanho_pilha, int detached) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, tamanho_pilha);
    pthread_attr_setdetachstate(&attr, detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE); 
    return attr;
}

void imprimir_tamanho_pilha(const char *nome) {
    pthread_attr_t attr;
    size_t tamanho_pilha;

    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &tamanho_pilha);
    printf("%s: Tamanho da pilha = %zu bytes\n", nome, tamanho_pilha);
    pthread_attr_destroy(&attr);
}

void *tarefa_processamento(void *arg) {
    imprimir_tamanho_pilha("Thread Processamento");
    printf("Thread de processamento executando...\n");
    for (long i=0; i<999999999; i++); // simula um processamento pesado
    printf("...\n");
    sleep(20);
    printf("Thread de processamento finalizada.\n");
    return NULL;
}

void *tarefa_log(void *arg) {
    imprimir_tamanho_pilha("Thread log");
    printf("Thread de log executando...\n");
    int log_counter = 0;
    while (1) {
        usleep(300000);
        printf("Log: Thread de log realizou registro %d ...\n", ++log_counter);
    }
    printf("Thread de log finalizada.\n");
    return NULL;
}

int main() {
    pthread_t t_proc, t_log;

    pthread_attr_t attr_proc = criar_atributos(PILHA_MAIOR, NOT_DETACHED);
    pthread_create(&t_proc, &attr_proc, tarefa_processamento, NULL);
    pthread_attr_destroy(&attr_proc);

    pthread_attr_t attr_log = criar_atributos(PILHA_MENOR, DETACHED);
    pthread_create(&t_log, &attr_log, tarefa_log, NULL);
    pthread_attr_destroy(&attr_log);

    pthread_join(t_proc, NULL); 
    sleep(1);
    printf("Programa principal finalizado.\n");

    return 0;
}