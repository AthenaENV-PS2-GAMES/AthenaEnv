# Pico e falhas de alocação — 2026-10-07

`System.getMemoryStats()` expõe agora `allocsPeak` e `allocationFailures`.
O pico é o maior total de bytes úteis vivos registrado pelo wrapper nativo
desde a inicialização. A contagem incrementa em pedidos não nulos de malloc,
calloc ou memalign que falham, e em realloc de tamanho positivo que falha.
Pedidos de tamanho zero e `realloc(p, 0)` não contam como falha.

A tela `bin/tests/memory_stats.js` exibe pico e falhas e valida os valores
antes de iniciar. A captura foi inspecionada: heap atual e pico aparecem
separados e o contador de falhas está visível. O ELF release registrou
`MEMORY_STATS: PASS native peak=431316 allocation failures=0` no PCSX2.

`tests/host/run_memory.sh` passou com ASan/UBSan/LeakSanitizer. O teste injeta
quatro falhas (malloc, calloc, memalign e realloc), confere o incremento exato,
verifica que OOM preserva bytes vivos, e cobre crescimento, redução e
`realloc(p, 0)`. Builds Docker debug/release terminaram com exit code 0.

- [Captura PCSX2](pcsx2-memory-stats.png)
- [Marcador e hashes dos ELFs](results.json)
- [Log dos builds Docker](docker-build.log)

O pico é global e inclui os blocos usados pelo QuickJS; `jsHeap` continua sendo
a parte estimada pelo QuickJS. `allocationFailures` é uma contagem cumulativa
que não é zerada. A leitura não inclui overhead do heap, fragmentação, stacks
ou VRAM. Atribuição por subsistema segue pendente porque exige manter domínio
por bloco durante realloc/free; o chamador de realloc sozinho não identifica
a quem o bloco pertence. Esta etapa não fez benchmark de performance.
