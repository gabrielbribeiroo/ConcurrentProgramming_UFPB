# Relatório de Implementação: Contador de Primos Concorrente

**Aluno:** Gabriel Barbosa Ribeiro de Oliveira

**Matrícula:** 20230012814

**Disciplina:** Linguagem de Programação II (Programação Concorrente)

**Data:** 15/12/2025

---

## 1. Introdução (Parte A)

Este trabalho tem como objetivo implementar e analisar um programa que conta a quantidade de números primos no intervalo \([2, N]\), comparando uma versão **sequencial** com versões **concorrentes** baseadas em múltiplos processos POSIX (`fork`), utilizando diferentes mecanismos de Comunicação entre Processos (IPC).

O foco do experimento é avaliar desempenho, consumo de recursos e explicar os resultados observados com base em métricas reais.

---

## 2. Especificação do Problema (Parte A)

### Entrada

O programa recebe os seguintes argumentos pela linha de comando:

- `MODE`: `seq` ou `par`
- `N`: inteiro ≥ 2 (limite superior do intervalo)
- `P`: inteiro ≥ 1 (apenas no modo `par`)
- `IPC`: `pipe` ou `shm` (apenas no modo `par`)
- `--algo basic` (opcional)

### Exemplos

```bash
./primecount seq 5000000
./primecount par 5000000 4 pipe
./primecount par 5000000 4 shm
```

### Saída

O programa imprime uma única linha contendo:

- modo de execução
- valor de N
- número de processos (se aplicável)
- método de IPC (se aplicável)
- quantidade de primos encontrados
- tempo total de execução em milissegundos

---

## 3. Algoritmo de Primalidade (Parte B)

Foi utilizado o método básico de teste de primalidade:

- números menores que 2 não são primos
- 2 é primo
- números pares maiores que 2 não são primos
- teste de divisores ímpares de 3 até √n

Esse algoritmo é CPU-bound e adequado para avaliar paralelismo.

---

## 4. Implementação (Partes B, C e D)

### 4.1 Versão Sequencial

A versão sequencial percorre todos os números de 2 até N, testando individualmente se cada número é primo e acumulando o total.

### 4.2 Versão Concorrente

- Um processo master divide o intervalo \([2, N]\) em `P` subintervalos contíguos.
- O master cria `P` processos worker usando `fork()`.
- Cada worker conta os primos em seu subintervalo.
- Os resultados são combinados pelo processo master.

### Comunicação entre Processos

- **Pipe:** cada worker envia sua contagem parcial ao master via `write/read`.
- **Shared Memory:** cada worker escreve sua contagem em uma posição de um vetor compartilhado criado com `mmap`.

---

## 5. Compilação e Execução (Parte A)

### Compilação

Você pode compilar manualmente com as mesmas flags usadas no `Makefile`:

```bash
g++ -O3 -std=c++17 -Wall -Wextra -pedantic primecount.cpp -o primecount
```

### Execução

```bash
./primecount seq 5000000
./primecount par 5000000 4 pipe
./primecount par 5000000 4 shm
```

### Também está disponível o nosso Makefile

O `Makefile` já está configurado com essas mesmas opções de compilação (`-O3 -std=c++17 -Wall -Wextra -pedantic`).

- Use para compilar

```bash
make
```

- Para testar

```bash
make run-seq
make run-pipe
make run-shm
```

---

## 6. Metodologia Experimental

- Valor testado: **N = 5.000.000**
- Cada configuração foi executada **3 vezes**
- O tempo considerado foi o **menor wall-clock**
- As medições de tempo e recursos foram feitas com:

```bash
/usr/bin/time -v
```

---

## 7. Resultados Experimentais (Parte D)

### Tabela de Resultados

| Modo | N   | P   | IPC  | Primos | Wall Time (s) | User (s) | Sys (s) | Max RSS (KB) |
| ---- | --- | --- | ---- | ------ | ------------- | -------- | ------- | ------------ |
| seq  | 5e6 | –   | –    | 348513 | 1.08          | 1.14     | 0.00    | 3584         |
| par  | 5e6 | 4   | pipe | 348513 | 0.67          | 2.13     | 0.01    | 3712         |
| par  | 5e6 | 4   | shm  | 348513 | 0.58          | 1.82     | 0.00    | 3584         |

---

## 8. Speedup (Parte D)

O speedup foi calculado como:

$$Speedup = \frac{T_{seq}}{T_{par}}$$

- **Pipe (P=4):**
  $$\frac{1.08}{0.67} \approx 1.61\times$$

- **Shared Memory (P=4):**
  $$\frac{1.08}{0.58} \approx 1.86\times$$
  
---

## 9. Análise e Explicação (Parte E)

Esta seção foi estruturada para atender explicitamente aos itens solicitados na **Parte E** do enunciado:

- **Item (i)**: cálculo e apresentação do *speedup* em relação à versão sequencial (Seção 8).
- **Item (ii)**: explicação de por que o *speedup* não é linear (Seção 9.2).
- **Item (iii)**: discussão sobre consumo de memória/recursos e efeitos de *Copy-on-Write* (Seção 9.3).
- **Item (iv)**: comparação entre os mecanismos de IPC `pipe` e `shm`, relacionando com as métricas observadas (Seção 9.4).

### 9.1 Speedup Observado

O uso de paralelismo trouxe ganho significativo de desempenho. Entretanto, o speedup ficou abaixo do ideal linear (4×).

### 9.2 Por que o Speedup não é Linear?

Os principais fatores observados foram:

- Overhead de criação e sincronização de processos (`fork`, `wait`)
- Desbalanceamento de carga, pois o teste de primalidade é mais custoso para números maiores
- Limitações de hardware, escalonamento e contenção de cache

### 9.3 Consumo de Memória

O consumo de memória permaneceu praticamente constante entre as versões.

Isso ocorre devido ao mecanismo de **Copy-on-Write (COW)** do Linux, que permite que os processos compartilhem páginas de memória enquanto não há escrita.

### 9.4 Pipe vs Shared Memory

A versão com memória compartilhada apresentou melhor desempenho:

- Pipes envolvem chamadas de sistema e cópia kernel↔user
- Shared memory permite acesso direto à RAM após o setup inicial
- Isso explica o menor tempo total e menor sys time observado no SHM

---

## 10. Conclusão

O trabalho demonstrou que o paralelismo com processos é eficaz para acelerar problemas CPU-bound.  
A escolha do mecanismo de IPC impacta o desempenho, com a memória compartilhada apresentando melhores resultados neste experimento.

Todos os objetivos propostos no enunciado foram atendidos, incluindo implementação, medição de desempenho e análise baseada em métricas reais.

---

## 11. Referências

- Material da disciplina LPII – Programação Concorrente
- `man fork`, `man pipe`, `man mmap`