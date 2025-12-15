# Makefile - Exercício Prático 001
# LPII - Programação Concorrente (2025.2)
# Aluno: Gabriel Barbosa Ribeiro de Oliveira
# Matrícula: 20230012814

# Compilador C++ a ser utilizado
CXX := g++

# Opções de compilação:
# -O3       : nível alto de otimização
# -std=c++17: usa padrão C++17
# -Wall     : habilita todos os avisos básicos
# -Wextra   : habilita avisos extras
# -pedantic : força conformidade estrita ao padrão C++
CXXFLAGS := -O3 -std=c++17 -Wall -Wextra -pedantic

# Nome do executável gerado
TARGET := primecount

# Arquivo-fonte principal do projeto
SRC := primecount.cpp

# Declaração de alvos "falsos" (não são arquivos reais),
# para evitar conflitos com arquivos de mesmo nome no diretório
.PHONY: all clean run-seq run-pipe run-shm

# Alvo padrão: compila o executável
all: $(TARGET)

# Regra de compilação: gera o executável a partir do arquivo-fonte
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Executa a versão sequencial da contagem de primos.
# Argumentos: modo seq, limite superior 5000000
run-seq: $(TARGET)
	@echo "--- Executando Sequencial ---"
	/usr/bin/time -v ./$(TARGET) seq 5000000

# Executa a versão paralela usando PIPE entre processos.
# Argumentos: modo par, limite 5000000, 4 processos, mecanismo pipe
run-pipe: $(TARGET)
	@echo "--- Executando Paralelo (PIPE) ---"
	/usr/bin/time -v ./$(TARGET) par 5000000 4 pipe

# Executa a versão paralela usando memória compartilhada (SHM).
# Argumentos: modo par, limite 5000000, 4 processos, mecanismo shm
run-shm: $(TARGET)
	@echo "--- Executando Paralelo (SHM) ---"
	/usr/bin/time -v ./$(TARGET) par 5000000 4 shm

# Remove o executável gerado
clean:
	rm -f $(TARGET)