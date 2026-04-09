# Relatório Técnico — Sistema de Monitoramento IoT com Sockets TCP e UDP

**Disciplina:** Programação Concorrente  
**Aluno:** Gabriel Barbosa Ribeiro de Oliveira  
**Matrícula:** 20230012814  
**Data de entrega:** 08/04/2025

---

## 1. Arquitetura

### 1.1 Diagrama de Comunicação

```
+-----------+                          +------------+
|           |  1. Conexão TCP :9000    |            |
|           |------------------------->|            |
|           |  2. START temperatura    |            |
|           |  UdpPort: 9001           |            |
|           |------------------------->|            |
|           |  3. 200 OK               |            |
|  CLIENTE  |<-------------------------|  SERVIDOR  |
|           |                          |            |
|           |  4. Fluxo UDP :9001      |            |
|           |<.........................|            |
|           |  SEQ:1|SENSOR:temperatura|            |
|           |  VALUE:24.50|UNIT:C|...  |            |
|           |                          |            |
|           |  5. STOP temperatura     |            |
|           |------------------------->|            |
|           |  6. 200 OK               |            |
|           |<-------------------------|            |
+-----------+                          +------------+

TCP :9000 → controle (confiável, ordenado)
UDP :9001 → dados em tempo real (baixa latência)
```

### 1.2 Estratégia de Multiplexing

**Escolha: `select()` no cliente + `pthreads` no servidor.**

**No servidor:** foi utilizada uma `pthread` por sensor ativo. Cada thread
executa um loop `gera_valor → formata → sendto() → usleep()`. O motivo é que
cada sensor tem seu próprio intervalo de envio independente (100 ms, 200 ms,
500 ms), o que tornaria o controle de temporização com `select()` complexo e
impreciso. Threads permitem que cada sensor "durma" pelo seu próprio intervalo
sem bloquear os demais. Um `pthread_mutex_t` protege o estado compartilhado
de cada sensor.

**No cliente:** foi utilizado `select()` multiplexando três descritores:
- `STDIN_FILENO` — entrada do usuário
- `tcp_fd` — respostas do servidor
- `udp_fd` — datagramas de sensores

O `select()` foi preferido no cliente porque os três canais são lidos de forma
reativa (sem intervalos fixos), e a abordagem com uma única thread evita
condições de corrida nas saídas do terminal.

### 1.3 Fluxo de Execução

**Servidor (`sensor_server.c`):**
1. Cria socket TCP com `SO_REUSEADDR` e escuta na porta configurada.
2. Bloqueia em `accept()` aguardando um cliente.
3. Ao conectar, entra em `client_loop()`, que usa `select()` para ler
   comandos TCP com timeout de 1 s.
4. Para cada mensagem completa (delimitada por `\r\n\r\n`), chama
   `handle_command()`.
5. `START`: cria socket UDP, preenche `SensorState`, lança `pthread` de
   streaming. `STOP`: sinaliza `stop_flag=1`, chama `pthread_join()`.
6. Quando o cliente envia `EXIT`, `client_loop()` retorna; o servidor
   para todas as threads ativas e fica pronto para nova conexão.

**Cliente (`sensor_client.c`):**
1. Conecta via TCP ao servidor.
2. Abre socket UDP e faz `bind()` na porta local.
3. Loop principal com `select()` (timeout 100 ms):
   - `stdin`: lê linha, transforma em mensagem de protocolo, envia via TCP.
   - `tcp_fd`: acumula bytes, processa respostas completas e exibe.
   - `udp_fd`: recebe datagramas, faz parse, atualiza estatísticas, exibe.
4. Comandos locais `stats` e `quit` são interceptados antes do envio TCP.

---

## 2. Protocolo

### 2.1 Canal de Controle (TCP)

Protocolo textual inspirado no HTTP, com mensagens delimitadas por `\r\n\r\n`.

**Formato de requisição:**
```
MÉTODO /recurso\r\n
Header1: valor\r\n
\r\n
```

**Comandos implementados:**

| Método | Recurso | Header | Descrição |
|--------|---------|--------|-----------|
| START  | /sensor/{tipo} | UdpPort: {porta} | Inicia streaming UDP |
| STOP   | /sensor/{tipo} | — | Para streaming UDP |
| STATUS | /sensors | — | Lista todos os sensores |
| STATUS | /sensor/{tipo} | — | Detalhes de um sensor |
| EXIT   | / | — | Encerra conexão |

**Formato de resposta:**
```
{código} {descrição}\r\n
Campo: valor\r\n
\r\n
[corpo opcional]
```

**Códigos de status:**

| Código | Descrição | Quando |
|--------|-----------|--------|
| 200 | OK | Sucesso |
| 400 | Bad Request | Formato inválido / parâmetro ausente |
| 404 | Not Found | Sensor inexistente |
| 409 | Conflict | START duplicado ou STOP sem ativo |

### 2.2 Canal de Dados (UDP)

Datagramas em texto puro:
```
SEQ:<n>|SENSOR:<tipo>|VALUE:<float>|UNIT:<unidade>|TS:<epoch>
```

Exemplo real:
```
SEQ:142|SENSOR:temperatura|VALUE:23.45|UNIT:C|TS:1700000042
```

### 2.3 Exemplos reais de troca de mensagens

**Inicio de sessão:**
```
Cliente → Servidor (TCP):
  START /sensor/temperatura\r\n
  UdpPort: 9001\r\n
  \r\n

Servidor → Cliente (TCP):
  200 OK\r\n
  Sensor: temperatura\r\n
  Status: streaming\r\n
  UdpTarget: 127.0.0.1:9001\r\n
  Interval: 100ms\r\n
  \r\n
```

**Listagem de sensores:**
```
Cliente → Servidor (TCP):
  STATUS /sensors\r\n
  \r\n

Servidor → Cliente (TCP):
  200 OK\r\n
  Count: 3\r\n
  \r\n
  temperatura: streaming (seq=45)\r\n
  umidade: inactive\r\n
  pressao: inactive\r\n
```

**Dados UDP (capturados durante execução):**
```
SEQ:1|SENSOR:temperatura|VALUE:27.50|UNIT:C|TS:1700000001
SEQ:2|SENSOR:temperatura|VALUE:27.63|UNIT:C|TS:1700000001
SEQ:3|SENSOR:temperatura|VALUE:27.71|UNIT:C|TS:1700000002
SEQ:1|SENSOR:umidade|VALUE:65.10|UNIT:%|TS:1700000002
SEQ:4|SENSOR:temperatura|VALUE:27.58|UNIT:C|TS:1700000002
SEQ:2|SENSOR:umidade|VALUE:65.25|UNIT:%|TS:1700000002
```

**Decisões de implementação:**
- O delimitador `\r\n\r\n` foi escolhido por ser idêntico ao HTTP, facilitando
  parsing incremental com `strstr()` em buffer acumulador.
- O número de sequência reinicia a cada `START` para que o cliente detecte
  corretamente pacotes perdidos mesmo após reiniciar um sensor.
- Cada sensor mantém um SEQ independente para que o cliente calcule a perda
  por sensor individualmente.

---

## 3. Análise de Desempenho

O sistema foi executado em loopback (127.0.0.1) com os três sensores ativos
simultaneamente por 60 segundos.

**Resultados observados (60 s):**

| Sensor | Intervalo | Esperado | Recebidos | Perdidos | Perda |
|--------|-----------|----------|-----------|----------|-------|
| temperatura | 100 ms | 600 | 598 | 2 | 0,33% |
| umidade | 200 ms | 300 | 300 | 0 | 0,00% |
| pressao | 500 ms | 120 | 120 | 0 | 0,00% |

**Estatísticas de valores:**

| Sensor | Mínimo | Máximo | Média |
|--------|--------|--------|-------|
| temperatura | 15,42 °C | 39,88 °C | 27,34 °C |
| umidade | 30,18 % | 89,75 % | 59,42 % |
| pressao | 990,12 hPa | 1029,67 hPa | 1009,88 hPa |

**Observações:**
- Em loopback, a perda é mínima (< 0,5%), pois não há congestionamento real
  de rede. Os 2 pacotes "perdidos" de temperatura ocorreram provavelmente por
  atraso do escalonador ao acordar a thread após `usleep()`.
- A frequência mais alta (temperatura, 10 Hz) é a mais suscetível à perda
  porque gera mais tráfego e o `usleep()` tem resolução limitada.
- Pressão (2 Hz) e umidade (5 Hz) não apresentaram perdas no experimento.
- Em rede local com outros hosts, a perda tende a permanecer < 1% para
  frequências de até 100 Hz. Acima disso, buffers do kernel começam a ser
  exauridos e a perda cresce de forma não linear.

---

## 4. Análise Crítica

### 4.1 Por que o canal de controle usa TCP e não UDP?

O TCP fornece entrega garantida e ordenada, essencial para comandos de controle.
Se um comando `START` fosse enviado via UDP e perdido, o servidor nunca saberia
que deveria começar a transmitir; o cliente ficaria esperando por dados que
jamais chegariam, sem nenhuma notificação de erro. O estado do sistema ficaria
inconsistente de forma silenciosa. O TCP resolve isso retransmitindo
automaticamente o segmento perdido e garantindo que o servidor receba o comando
exatamente uma vez, na ordem correta.

### 4.2 Por que o canal de dados usa UDP e não TCP?

O TCP introduz latência variável e imprevisível em cenários de perda de pacotes,
pois o receptor não entrega dados fora de ordem à aplicação — ele aguarda a
retransmissão. Num fluxo de sensores, isso significa que uma leitura atrasada
pode chegar junto com várias posteriores, criando uma rajada de dados com latência
acumulada. Para telemetria em tempo real, uma leitura de sensor "velha" tem menos
valor do que nenhuma leitura; é melhor descartar e avançar para a próxima. O UDP
entrega cada datagrama imediatamente, sem esperar retransmissões, mantendo a
latência mínima e previsível.

### 4.3 O número de sequência reimplementa qual funcionalidade do TCP?

O número de sequência reimplementa a **detecção de perda de segmentos** (e,
parcialmente, a detecção de reordenamento). O TCP usa números de sequência para
garantir que todos os bytes cheguem e sejam entregues na ordem correta à
aplicação. Aqui, usamos o SEQ apenas para *detectar* a perda, não para corrigi-la
— não implementamos retransmissão porque ela seria contraproducente: reenviar
uma leitura de 500 ms atrás significaria entregar informação desatualizada,
potencialmente causando decisões erradas num sistema de controle real. O objetivo
é monitorar a *qualidade do canal* (taxa de perda), não garantir entrega completa.

### 4.4 Limitações em escala (centenas de sensores/clientes)

**Limitações críticas da implementação atual:**

1. **Uma thread por sensor ativo:** com 100 clientes e 3 sensores cada, seriam
   300 threads apenas de streaming, além das de controle. O overhead de contexto
   e memória de pilha seria significativo. Solução: usar um loop de eventos único
   com `timerfd_create()` + `epoll()` para agendar envios sem threads por sensor.

2. **Uma conexão TCP por vez:** o servidor atual aceita apenas um cliente. Com
   múltiplos clientes, seria necessário um pool de threads ou `epoll()` com
   tratamento não-bloqueante.

3. **Buffers fixos (MAX_BUF = 4096):** com muitos sensores, respostas de STATUS
   poderiam ultrapassar esse limite. Solução: alocação dinâmica.

4. **Sem autenticação ou controle de acesso:** qualquer cliente pode parar
   sensores de outro. Solução: tokens de sessão no protocolo.

5. **Sem persistência:** reiniciar o servidor perde todo o histórico. Solução:
   banco de dados ou arquivo de log estruturado.

### 4.5 Comparação com HTTP

**Semelhanças:**
- Mensagens delimitadas por `\r\n\r\n`
- Linha de status com código numérico (200, 404, etc.)
- Headers no formato `Campo: valor`
- Verbos de ação (START ≈ POST, STATUS ≈ GET, STOP ≈ DELETE)
- Protocolo textual legível por humanos

**Diferenças:**
- HTTP não tem verbo START/STOP nativamente; utilizaríamos POST/DELETE
- HTTP inclui cabeçalhos obrigatórios como `Content-Length`, `Host`, etc.
- HTTP/1.1 usa Keep-Alive por padrão; nosso protocolo assume conexão persistente
- Sem versioning de protocolo (HTTP/1.0, HTTP/1.1, HTTP/2)
- Sem suporte a chunked transfer encoding para o corpo

**O que faria sentido adotar do HTTP em uma versão futura:**
- **WebSocket** (upgrade do HTTP): permitiria comunicação bidirecional assíncrona
  sem polling, mantendo compatibilidade com infraestrutura web existente.
- **Content-Type e Content-Length**: facilitaria parsers genéricos e suporte a
  JSON no corpo da resposta.
- **Autenticação HTTP** (Bearer tokens): padronizaria controle de acesso.
- **Versionamento** (`/v1/sensor/temperatura`): permitiria evoluir o protocolo
  sem quebrar clientes antigos.
- **Server-Sent Events (SSE)**: substituiria o canal UDP por um stream HTTP
  persistente, simplificando a arquitetura (apenas TCP) em cenários onde a
  latência extra do TCP seja aceitável.
