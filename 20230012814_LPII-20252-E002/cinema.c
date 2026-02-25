/**
 * cinema.c — Sistema de Reserva de Assentos em Cinema
 * Exercício Prático 002 — Programação Concorrente (LPII 2025.2)
 *
 * Aluno: 20230012814
 *
 * Compilar: gcc -o cinema cinema.c -lpthread
 * Executar: ./cinema
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


#define FILEIRAS 5
#define ASSENTOS_POR_FILEIRA 10
#define NUM_CLIENTES 20

typedef struct {
  int assentos[FILEIRAS][ASSENTOS_POR_FILEIRA];
  int reservas_sucesso;
  int reservas_falha;
} Sala;

Sala sala;

/* Mutex por fileira — protege cada fileira individualmente */
pthread_mutex_t mutex_fileira[FILEIRAS];

/* Mutex para os contadores de estatísticas */
pthread_mutex_t mutex_estatisticas;

/* ------------------------------------------------------------------ */
/* inicializar_sala: zera todos os assentos e contadores              */
/* ------------------------------------------------------------------ */
void inicializar_sala() {
  for (int i = 0; i < FILEIRAS; i++) {
    for (int j = 0; j < ASSENTOS_POR_FILEIRA; j++) {
      sala.assentos[i][j] = 0; /* 0 = livre */
    }
  }
  sala.reservas_sucesso = 0;
  sala.reservas_falha = 0;
}

/* ------------------------------------------------------------------ */
/* tentar_reserva: busca par de assentos livres na fileira indicada.  */
/* Retorna 1 se conseguiu reservar, 0 caso contrário.                */
/* SEÇÃO CRÍTICA: protegida por mutex da fileira correspondente.     */
/* ------------------------------------------------------------------ */
int tentar_reserva(int cliente_id, int fileira) {
  pthread_mutex_lock(&mutex_fileira[fileira]);

  /* Busca dois assentos adjacentes livres */
  for (int j = 0; j < ASSENTOS_POR_FILEIRA - 1; j++) {
    if (sala.assentos[fileira][j] == 0 && sala.assentos[fileira][j + 1] == 0) {
      /* Simula latência de processamento */
      usleep(rand() % 1000);

      /* Reserva o par de assentos, marcando com o ID do cliente */
      sala.assentos[fileira][j] = cliente_id;
      sala.assentos[fileira][j + 1] = cliente_id;

      pthread_mutex_unlock(&mutex_fileira[fileira]);
      return 1; /* Reserva bem-sucedida */
    }
  }

  pthread_mutex_unlock(&mutex_fileira[fileira]);
  return 0; /* Nenhum par disponível nesta fileira */
}

/* ------------------------------------------------------------------ */
/* cliente: função executada por cada thread cliente.                 */
/* Tenta reservar em fileiras aleatórias; se nenhuma funcionar,      */
/* desiste.                                                           */
/* ------------------------------------------------------------------ */
void *cliente(void *arg) {
  int cliente_id = *((int *)arg);

  /* Gera uma ordem aleatória de fileiras para tentar */
  int ordem[FILEIRAS];
  for (int i = 0; i < FILEIRAS; i++) {
    ordem[i] = i;
  }
  /* Embaralha (Fisher-Yates) */
  for (int i = FILEIRAS - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int tmp = ordem[i];
    ordem[i] = ordem[j];
    ordem[j] = tmp;
  }

  /* Tenta cada fileira na ordem aleatória */
  for (int i = 0; i < FILEIRAS; i++) {
    if (tentar_reserva(cliente_id, ordem[i])) {
      printf("Cliente %2d: reservou par na fileira %d\n", cliente_id,
             ordem[i] + 1);

      /* Atualiza contador de sucesso (seção crítica) */
      pthread_mutex_lock(&mutex_estatisticas);
      sala.reservas_sucesso++;
      pthread_mutex_unlock(&mutex_estatisticas);

      return NULL;
    }
  }

  /* Não conseguiu em nenhuma fileira */
  printf("Cliente %2d: não conseguiu reservar — desistiu\n", cliente_id);

  /* Atualiza contador de falha (seção crítica) */
  pthread_mutex_lock(&mutex_estatisticas);
  sala.reservas_falha++;
  pthread_mutex_unlock(&mutex_estatisticas);

  return NULL;
}

/* ------------------------------------------------------------------ */
/* imprimir_sala: exibe mapa visual da sala.                          */
/* "." = livre, número/letra do cliente = ocupado.                   */
/* ------------------------------------------------------------------ */
void imprimir_sala() {
  printf("\n========== MAPA DA SALA ==========\n");
  printf("         ");
  for (int j = 0; j < ASSENTOS_POR_FILEIRA; j++) {
    printf(" %2d", j + 1);
  }
  printf("\n");

  for (int i = 0; i < FILEIRAS; i++) {
    printf("Fileira %d:", i + 1);
    for (int j = 0; j < ASSENTOS_POR_FILEIRA; j++) {
      if (sala.assentos[i][j] == 0) {
        printf("  .");
      } else {
        printf(" %2d", sala.assentos[i][j]);
      }
    }
    printf("\n");
  }
  printf("==================================\n");
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main() {
  srand(time(NULL));

  /* Inicializa mutexes */
  for (int i = 0; i < FILEIRAS; i++) {
    pthread_mutex_init(&mutex_fileira[i], NULL);
  }
  pthread_mutex_init(&mutex_estatisticas, NULL);

  /* Inicializa sala */
  inicializar_sala();

  /* Cria threads de clientes */
  pthread_t threads[NUM_CLIENTES];
  int ids[NUM_CLIENTES];

  printf("=== Sistema de Reserva de Cinema ===\n");
  printf("Fileiras: %d | Assentos por fileira: %d | Clientes: %d\n\n", FILEIRAS,
         ASSENTOS_POR_FILEIRA, NUM_CLIENTES);

  for (int i = 0; i < NUM_CLIENTES; i++) {
    ids[i] = i + 1; /* IDs de 1 a 20 */
    pthread_create(&threads[i], NULL, cliente, &ids[i]);
  }

  /* Aguarda todas as threads terminarem */
  for (int i = 0; i < NUM_CLIENTES; i++) {
    pthread_join(threads[i], NULL);
  }

  /* Exibe resultados */
  imprimir_sala();

  /* Conta assentos ocupados para verificação */
  int total_ocupados = 0;
  for (int i = 0; i < FILEIRAS; i++) {
    for (int j = 0; j < ASSENTOS_POR_FILEIRA; j++) {
      if (sala.assentos[i][j] != 0) {
        total_ocupados++;
      }
    }
  }

  printf("\n========== ESTATÍSTICAS ==========\n");
  printf("Total de assentos reservados: %d / %d\n", total_ocupados,
         FILEIRAS * ASSENTOS_POR_FILEIRA);
  printf("Clientes atendidos (sucesso): %d\n", sala.reservas_sucesso);
  printf("Clientes que desistiram:      %d\n", sala.reservas_falha);
  printf("==================================\n");

  /* Verificação de integridade */
  printf("\n=== Verificação de Integridade ===\n");
  if (total_ocupados == 2 * sala.reservas_sucesso) {
    printf("OK: assentos ocupados (%d) = 2 × clientes atendidos (%d)\n",
           total_ocupados, sala.reservas_sucesso);
  } else {
    printf("ERRO: assentos ocupados (%d) != 2 × clientes atendidos (%d)\n",
           total_ocupados, sala.reservas_sucesso);
  }

  printf("Total de clientes: %d (sucesso + falha = %d)\n", NUM_CLIENTES,
         sala.reservas_sucesso + sala.reservas_falha);

  /* Destrói mutexes */
  for (int i = 0; i < FILEIRAS; i++) {
    pthread_mutex_destroy(&mutex_fileira[i]);
  }
  pthread_mutex_destroy(&mutex_estatisticas);

  return 0;
}
