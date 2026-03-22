# Relatório: Simulador de Restaurante Concorrente
**Disciplina:** LPII - Programação Concorrente (2025.2)  
**Professor:** Carlos Eduardo Batista  
**Aluno:** Gabriel (Matrícula: 20230012814)

---

## 1. Identificação
- **Cenário Escolhido:** Cenário A - Restaurante Concorrente.
- **Parâmetros Personalizados (M = 2814):**
  - `NUM_COZINHEIROS = (2814 % 4) + 2 = 2 + 2 = 4`
  - `NUM_FOGOES = (2814 % 3) + 2 = 0 + 2 = 2`
  - `NUM_MESAS = ((28114 / 10) % 5) + 4 = 1 + 4 = 5`
  - `NUM_GARCONS = ((2814 / 100) % 3) + 2 = 1 + 2 = 3`
  - `TEMPO_PREPARO = ((2814 / 100) % 3) + 1 = 1 + 1 = 2 segundos`

---

## 2. Decisões de Projeto
Para a implementação deste simulador, foram utilizados três mecanismos principais de sincronização POSIX:

1.  **Semáforos Contadores (`sem_t`)**:
    - **Acesso aos Fogões**: Um semáforo inicializado com `NUM_FOGOES` (2) garante que no máximo 2 cozinheiros usem os fogões simultaneamente, evitando condições de corrida sobre o recurso limitado.
    - **Pool de Atendimento e Entrega**: Semáforos individuais para cada mesa permitem que os clientes sinalizem a necessidade de atendimento e aguardem especificamente seu prato.
    - **Produtor-Consumidor**: Semáforos `sem_vagas_fila` e `sem_pedidos_fila` controlam o fluxo de pedidos entre Garçons (produtores) e Cozinheiros (consumidores).

2.  **Mutexes (`pthread_mutex_t`)**:
    - **Buffer Compartilhado**: Protege o acesso à `fila_pedidos` durante as operações de inserção e remoção, garantindo a integridade dos índices `head` e `tail`.
    - **Estatísticas Globais**: Garante que incrementos em contadores de pedidos atendidos e somas de tempo sejam atômicos.

3.  **Barreiras (`pthread_barrier_t`)**:
    - Utilizada para sincronizar o início das mesas, simulando que todas as mesas "sentam" e ficam prontas para pedir simultaneamente, evidenciando a concorrência pelo atendimento dos garçons.

**Justificativa:** Optou-se por semáforos para recursos limitados (fogões) por ser a abstração mais direta. Para o buffer de pedidos, a combinação Mutex + Semáforo é o padrão clássico e robusto para o problema Produtor-Consumidor.

---

## 3. Resultados Experimentais
Ao executar o programa, observa-se o seguinte comportamento:
- As 5 mesas chegam simultaneamente.
- Os 3 garçons começam a atender as mesas conforme disponibilidade.
- Como há apenas 2 fogões, mesmo com 4 cozinheiros, apenas 2 pedidos são preparados por vez.
- O log detalha cada etapa, mostrando garçons coletando pedidos e cozinheiros lutando pelos fogões.

**Estatísticas de Exemplo:**
- Pedidos Atendidos: 5
- Tempo Total: ~6 segundos (depende do escalonamento)
- OPS: ~0.83 pedidos/segundo
- Tempo Médio de Espera: ~3.00 s

---

## 4. Reflexão sobre Uso de IA
- **Ferramentas:** Antigravity AI (baseada no currículo da Google DeepMind).
- **Tarefas:** Geração da estrutura base, cálculo de parâmetros e auxílio na implementação de sincronização thread-safe.
- **Erros e Correções:** Identificou-se uma delevação acidental de variáveis globais durante um refactor, que foi prontamente corrigida após análise dos erros de linter.
- **Aprendizado:** A IA facilitou a estruturação de um ambiente concursal POSIX complexo em Windows (simulando ambiente Linux), permitindo focar na lógica de sincronização.
- **Validação:** O código foi revisado para garantir que todos os `sem_wait` tivessem seus respectivos `sem_post` para evitar Deadlocks.
