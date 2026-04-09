# Sistema de Monitoramento IoT com Sockets TCP e UDP

**Disciplina:** Programação Concorrente  
**Aluno:** Gabriel Barbosa Ribeiro de Oliveira  
**Matrícula:** 20230012814

---

## Compilação

```bash
make all       # compila servidor e cliente
make server    # compila apenas o servidor
make client    # compila apenas o cliente
make clean     # remove binários
```

Requisitos: `gcc`, `make`, Linux (Ubuntu 22.04+).

---

## Execução

**Terminal 1 — Servidor:**
```bash
./sensor_server [porta_tcp]
# Exemplo: ./sensor_server 9000
```

**Terminal 2 — Cliente:**
```bash
./sensor_client <ip_servidor> [porta_tcp] [porta_udp]
# Exemplo: ./sensor_client 127.0.0.1 9000 9001
```

Portas padrão: TCP=9000, UDP=9001.

---

## Comandos disponíveis no cliente

| Comando | Descrição |
|---------|-----------|
| `START temperatura` | Inicia fluxo UDP do sensor de temperatura |
| `START umidade` | Inicia fluxo UDP do sensor de umidade |
| `START pressao` | Inicia fluxo UDP do sensor de pressão |
| `STOP temperatura` | Para o fluxo do sensor de temperatura |
| `STOP umidade` | Para o fluxo do sensor de umidade |
| `STOP pressao` | Para o fluxo do sensor de pressão |
| `STATUS` | Lista todos os sensores e seus estados |
| `STATUS temperatura` | Detalha um sensor específico |
| `stats` | Exibe estatísticas locais acumuladas |
| `quit` | Encerra a conexão e o cliente |

---

## Exemplo de sessão de uso

```
$ ./sensor_client 127.0.0.1 9000 9001
Conectado ao servidor 127.0.0.1:9000
Socket UDP escutando na porta 9001
Digite comandos (START, STOP, STATUS, stats, quit):

> START temperatura

Resposta: 200 OK
Sensor: temperatura
Status: streaming
UdpTarget: 127.0.0.1:9001
Interval: 100ms

[UDP] SEQ:1 | temperatura  |  24.50 C   | TS:1700000001
[UDP] SEQ:2 | temperatura  |  24.63 C   | TS:1700000001
> START umidade

Resposta: 200 OK
Sensor: umidade
Status: streaming
UdpTarget: 127.0.0.1:9001
Interval: 200ms

[UDP] SEQ:3 | temperatura  |  24.71 C   | TS:1700000002
[UDP] SEQ:1 | umidade      |  65.10 %   | TS:1700000002
> STATUS

Resposta: 200 OK
Count: 3

temperatura: streaming (seq=45)
umidade: streaming (seq=22)
pressao: inactive

> stats

=== Estatisticas Locais ===
temperatura  : recebidos=44, perdidos=1 (2.2%), min=23.10, max=26.44, media=24.82
umidade      : recebidos=22, perdidos=0 (0.0%), min=63.20, max=67.55, media=65.32
===========================

> STOP temperatura

Resposta: 200 OK
Sensor: temperatura
Status: stopped

> quit
Enviando EXIT ao servidor...
Conexão encerrada.
```
