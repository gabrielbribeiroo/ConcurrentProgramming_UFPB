/**
 * =========================================================================================
 * INCLUSÃO DE BIBLIOTECAS PADRÕES E POSIX
 * =========================================================================================
 * Nesta seção estão todas as dependências utilizadas pelo programa, tanto
 * da biblioteca padrão C++ quanto das chamadas de sistema POSIX exigidas
 * para criação de processos e mecanismos de comunicação entre processos.
 */
#include <iostream>     // Entrada e saída padrão (std::cout, std::cerr)
#include <vector>       // Vetores dinâmicos (std::vector)
#include <string>       // Manipulação de strings (std::string)
#include <cmath>        // Funções matemáticas (sqrt)
#include <cstring>      // Manipulação de strings estilo C
#include <chrono>       // Medição de tempo de alta precisão
#include <unistd.h>     // Chamadas POSIX: fork, pipe, write, read, close
#include <sys/wait.h>   // Funções de espera por processos filhos (wait)
#include <sys/mman.h>   // mmap/munmap para memória compartilhada (SHM)

// ==========================================
// PARTE B: Lógica de Primalidade e Worker
// ==========================================

/**
 * Função: is_prime_basic
 * ----------------------
 * Verifica se um número inteiro 'n' é primo.
 * Algoritmo: Divisão por tentativas (trial division), testando apenas
 * divisores até a raiz quadrada de n.
 * Complexidade assintótica: O(sqrt(n)).
 */
bool is_prime_basic(int n) {
    // Casos base: números menores que 2 não são primos
    if (n < 2) return false;
    // 2 é o único número par primo
    if (n == 2) return true;
    // Elimina todos os outros pares rapidamente
    if (n % 2 == 0) return false;
    
    // Otimização: Testa apenas divisores ímpares de 3 até a raiz quadrada de n.
    // Se n tiver um fator maior que sqrt(n), o outro fator necessariamente é menor que sqrt(n).
    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

/**
 * Função: count_primes_interval
 * -----------------------------
 * Conta quantos números primos existem em um intervalo fechado [start, end].
 * Esta é a "tarefa de trabalho" que será executada tanto pelo modo
 * sequencial quanto pelos processos filhos (workers) no modo paralelo.
 */
long long count_primes_interval(int start, int end) {
    long long prime_count = 0;
    for (int i = start; i <= end; i++) {
        if (is_prime_basic(i)) {
            prime_count++;
        }
    }
    return prime_count;
}

// ==========================================
// Implementação SEQUENCIAL
// ==========================================

/**
 * Função: run_sequential
 * ----------------------
 * Executa a contagem de primos em um único processo (sem paralelismo).
 * Serve como linha de base (baseline) para comparar o ganho de desempenho
 * da versão concorrente.
 */
long long run_sequential(int N) {
    // Conta primos no intervalo completo [2, N]
    return count_primes_interval(2, N);
}

// ==========================================
// PARTE C: Implementação CONCORRENTE
// ==========================================

/**
 * Função: run_concurrent
 * ----------------------
 * Gerencia a execução concorrente utilizando múltiplos processos.
 * Passos principais:
 * 1. Divide o intervalo total de busca de primos entre os processos.
 * 2. Configura o mecanismo de comunicação (Pipe ou Memória Compartilhada).
 * 3. Cria processos filhos (fork), um por fatia de intervalo.
 * 4. Cada filho calcula a quantidade de primos em sua fatia.
 * 5. O processo pai sincroniza, coleta e agrega os resultados parciais.
 */
long long run_concurrent(int N, int P, const std::string& ipc_type) {
    // ---------------------------------------------------------
    // 1. Definição dos Intervalos (Balanceamento de Carga)
    // ---------------------------------------------------------
    // O intervalo de interesse é [2, N].
    int total_numbers = N - 1;            // Quantidade total de números a verificar
    int base_chunk_size = total_numbers / P; // Tamanho base da fatia para cada processo
    int extra_numbers = total_numbers % P;   // Números "a mais" distribuídos entre os primeiros processos

    // ---------------------------------------------------------
    // 2. Setup do IPC (Inter-Process Communication)
    // ---------------------------------------------------------
    int** ipc_pipes = nullptr;      // Matriz para armazenar descritores de arquivo (se usar Pipe)
    long long* shared_results = nullptr;  // Ponteiro para a memória compartilhada (se usar SHM)

    if (ipc_type == "pipe") {
        // Aloca array de ponteiros para os pipes
        ipc_pipes = new int*[P];
        for (int i = 0; i < P; i++) {
            ipc_pipes[i] = new int[2]; // Cada pipe tem 2 descritores: [0]=leitura, [1]=escrita
            // Cria o pipe. Se falhar, encerra.
            if (pipe(ipc_pipes[i]) == -1) {
                perror("Erro ao criar pipe");
                exit(1);
            }
        }
    } else if (ipc_type == "shm") {
        // Configura Memória Compartilhada usando mmap.
        // MAP_SHARED: As alterações são visíveis para outros processos mapeando a mesma região.
        // MAP_ANONYMOUS: A memória não é baseada em arquivo, é criada na RAM e zerada.
        // PROT_READ | PROT_WRITE: Permissão de leitura e escrita.
        size_t shm_size = sizeof(long long) * P; // Espaço para um contador (long long) por processo
        shared_results = (long long*)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, 
                                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        
        if (shared_results == MAP_FAILED) {
            perror("Erro no mmap");
            exit(1);
        }
    } else {
        std::cerr << "IPC invalido (use 'pipe' ou 'shm')" << std::endl;
        exit(1);
    }

    // ---------------------------------------------------------
    // 3. Loop de Criação de Processos (Fork)
    // ---------------------------------------------------------
    int current_start = 2; // Início do próximo subintervalo a ser atribuído

    for (int i = 0; i < P; i++) {
        // Calcula onde começa e termina o intervalo do processo 'i'
        // Distribui o 'remainder' dando 1 número extra para os primeiros processos
        int chunk_size = base_chunk_size + (i < extra_numbers ? 1 : 0);
        int current_end = current_start + chunk_size - 1;

        // Cria um novo processo duplicando o atual
        pid_t pid = fork();

        if (pid < 0) {
            perror("Erro no fork");
            exit(1);
        } 
        else if (pid == 0) {
            // =====================================================
            // CÓDIGO DO PROCESSO FILHO (WORKER)
            // =====================================================
            
            // Gerenciamento de Pipes no Filho
            if (ipc_type == "pipe") {
                for (int j = 0; j < P; j++) {
                    close(ipc_pipes[j][0]); // Filho NUNCA lê do pipe, fecha leitura
                    if (j != i) close(ipc_pipes[j][1]); // Fecha escrita dos pipes dos IRMÃOS
                }
            }

            // Realiza o trabalho pesado (CPU-bound)
            long long primes_found = count_primes_interval(current_start, current_end);

            // Envia o resultado para o Pai
            if (ipc_type == "pipe") {
                // Escreve o resultado binário no pipe dedicado a este processo (i)
                if (write(ipc_pipes[i][1], &primes_found, sizeof(long long)) == -1) {
                    perror("Erro na escrita do pipe");
                    exit(1);
                }
                close(ipc_pipes[i][1]); // Fecha a ponta de escrita após enviar (envia EOF)
            } else if (ipc_type == "shm") {
                // Escreve diretamente no slot do array compartilhado na memória
                shared_results[i] = primes_found; 
            }
            
            // Limpeza de memória alocada no heap (herdada do pai) para evitar vazamento no valgrind
            if (ipc_type == "pipe") {
                for(int k=0; k<P; k++) delete[] ipc_pipes[k];
                delete[] ipc_pipes;
            }

            exit(0); // OBRIGATÓRIO: Filho termina aqui para não continuar o loop do pai
        }

        // =====================================================
        // CÓDIGO DO PROCESSO PAI (MASTER) CONTINUAÇÃO DO LOOP
        // =====================================================
        
        // Se usar pipe, pai fecha a ponta de ESCRITA deste pipe específico.
        // Se o pai mantiver aberto, o 'read' nunca retornará 0 (EOF) se decidirmos ler em loop.
        if (ipc_type == "pipe") {
            close(ipc_pipes[i][1]);
        }

        // Atualiza o início para o próximo worker
        current_start = current_end + 1;
    }

    // ---------------------------------------------------------
    // 4. Sincronização e Agregação de Resultados
    // ---------------------------------------------------------
    long long total_primes = 0;

    // Passo A: Esperar TODOS os filhos terminarem.
    // Isso é crucial para evitar processos "zumbis" e garantir que todos calcularam.
    for (int i = 0; i < P; i++) {
        wait(NULL);
    }

    // Passo B: Coletar e somar os resultados parciais
    if (ipc_type == "pipe") {
        for (int i = 0; i < P; i++) {
            long long partial_primes = 0;
            // Lê do pipe. O read retorna > 0 se leu bytes.
            if (read(ipc_pipes[i][0], &partial_primes, sizeof(long long)) > 0) {
                total_primes += partial_primes;
            }
            close(ipc_pipes[i][0]); // Fecha a ponta de leitura
            delete[] ipc_pipes[i];  // Libera memória do par de inteiros
        }
        delete[] ipc_pipes; // Libera o array de ponteiros
    } else if (ipc_type == "shm") {
        // No caso de memória compartilhada, os dados já estão lá
        for (int i = 0; i < P; i++) {
            total_primes += shared_results[i];
        }
        // Desfaz o mapeamento da memória (limpeza)
        munmap(shared_results, sizeof(long long) * P);
    }

    return total_primes;
}

// ==========================================
// PARTE A: Main e Validação
// ==========================================

// Função auxiliar para exibir a forma correta de uso do programa
void print_usage(const char* prog_name) {
    std::cerr << "Uso:\n"
              << "  Sequencial: " << prog_name << " seq <N> [--algo basic]\n"
              << "  Paralelo:   " << prog_name << " par <N> <P> <IPC> [--algo basic]\n\n"
              << "Argumentos:\n"
              << "  N:    Inteiro >= 2\n"
              << "  P:    Inteiro >= 1\n"
              << "  IPC:  'pipe' ou 'shm'\n";
}

int main(int argc, char* argv[]) {
    // Validação mínima da quantidade de argumentos fornecidos
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // Leitura dos argumentos básicos obrigatórios
    std::string mode = argv[1];
    int N = 0;
    
    // Converte e valida N (limite superior da faixa de busca de primos)
    try {
        N = std::stoi(argv[2]);
    } catch (...) {
        std::cerr << "Erro: N deve ser inteiro." << std::endl;
        return 1;
    }
    if (N < 2) {
        std::cerr << "Erro: N deve ser >= 2." << std::endl;
        return 1;
    }

    int P = 0;
    std::string ipc = "none";
    std::string algo = "basic"; 

    int next_arg_idx = 3; // Índice para continuar a leitura de argumentos

    // Validações específicas para o modo Paralelo
    if (mode == "par") {
        if (argc < 5) {
            std::cerr << "Erro: Modo 'par' requer P e IPC." << std::endl;
            print_usage(argv[0]);
            return 1;
        }
        
        try {
            P = std::stoi(argv[3]);
        } catch (...) {
            std::cerr << "Erro: P deve ser inteiro." << std::endl;
            return 1;
        }
        if (P < 1) {
            std::cerr << "Erro: P deve ser >= 1." << std::endl;
            return 1;
        }

        ipc = argv[4];
        if (ipc != "pipe" && ipc != "shm") {
            std::cerr << "Erro: IPC deve ser 'pipe' ou 'shm'." << std::endl;
            return 1;
        }
        
        next_arg_idx = 5;
    } else if (mode != "seq") {
        std::cerr << "Erro: Modo desconhecido (use 'seq' ou 'par')." << std::endl;
        return 1;
    }

    // Busca argumento opcional --algo (se existir)
    for (int i = next_arg_idx; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--algo" && i + 1 < argc) {
            algo = argv[i + 1];
            i++;
        }
    }

    // ---------------------------------------------------------
    // Execução e Medição de Tempo
    // ---------------------------------------------------------
    
    // Captura o tempo inicial (Monotônico, imune a mudanças de horário do sistema)
    auto start_time = std::chrono::steady_clock::now();
    
    long long primes = 0;
    
    // Decide qual função executar com base no modo
    if (mode == "seq") {
        primes = run_sequential(N);
    } else {
        primes = run_concurrent(N, P, ipc);
    }

    // Captura tempo final e calcula a diferença em milissegundos
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // ---------------------------------------------------------
    // Saída Formatada (conforme requisitos do trabalho)
    // ---------------------------------------------------------
    std::cout << "mode=" << mode 
              << " N=" << N;
    
    if (mode == "par") {
        std::cout << " P=" << P 
                  << " ipc=" << ipc;
    }
    
    std::cout << " primes=" << primes 
              << " time_ms=" << elapsed_ms 
              << std::endl;

    return 0;
}