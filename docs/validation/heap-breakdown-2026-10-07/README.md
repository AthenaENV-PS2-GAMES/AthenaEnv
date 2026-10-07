# Métricas do heap EE — 2026-10-07

`System.getMemoryStats()` agora reporta o snapshot do `mallinfo` newlib: arena
reservada, chunks alocados/livres, contagem de chunks livres e espaço livre no
topo. `heapOverhead` estima a diferença entre chunks ocupados e os bytes úteis
contados pelo wrapper. `heapNonTopFree` reporta bytes livres fora do topo como
sinal de fragmentação.

No PCSX2 2.8.2, o ELF release executou `tests/memory_stats.js` sem erro:

```text
MEMORY_STATS: PASS native peak=442908 allocation failures=0
HEAP_BREAKDOWN: PASS arena=473768 allocated=462592 overhead=25448 free=11176 chunks=48 nonTopFree=8336
MEMORY_BUDGET: PASS native headroom=7000237 JS limit=21474175 invalid requests rejected
```

A [captura inspecionada](pcsx2-heap-breakdown.png) mostra arena, alocado,
overhead, bytes e chunks livres, espaço do topo e indicador de fragmentação,
além das métricas anteriores de heap JS, stack e VRAM.

`tests/host/run_memory.sh` passou com oito workers × 20000 ciclos, incluindo
realloc, OOM e interrupções previamente desabilitadas. `tests/host/run_safety.sh`
também passou, cobrindo DMA, fontes, listas 2D, MPG, page clear e Scene3D. O
teste host do allocator valida o mapeamento dos campos do `mallinfo` com valores
sintéticos. Os ELFs debug e release foram compilados via Docker em
[build.log](build.log).

ELF debug SHA-256:
`2c3c0e16ff1234da3f72c192bae5f97ee789686de5fbbb4723ba1e11123f7da5`

ELF release SHA-256:
`75d12bde46aae219c0935b9ae9d436e0065211f3e026c13b18a391538b84e559`

As métricas são globais e aproximadas. Workers podem alterar o heap entre as
leituras de `mallinfo` e do contador útil; os bytes de overhead incluem
metadados e alinhamento. `heapNonTopFree` é um sinal, não uma medida exata de
fragmentação inutilizável. Atribuição de bytes vivos por subsistema continua
pendente e exige preservar o domínio da alocação até o `free`/`realloc`.
