# Orçamento QuickJS e margem nativa — 2026-10-07

`System.setNativeMemoryHeadroom(bytes)` ajusta o teto absoluto do heap QuickJS
usando o heap JS atual e a RAM EE livre no momento. A chamada deixa pelo menos
64 KiB para o runtime e retorna o novo teto. `System.getMemoryStats().jsLimit`
mostra esse teto. O padrão inicial segue reservando metade da RAM livre para
QuickJS.

A margem é calculada por snapshot: allocations nativas futuras, inclusive de
workers, podem consumi-la. Chame em transições de fase, antes de carregar
assets, e recalibre depois de mudanças relevantes na carga. Ainda não há um
governor que acompanhe automaticamente crescimento concorrente.

Validação PCSX2 release: `MEMORY_BUDGET: PASS native headroom=7002175 JS limit=21473725 invalid
requests rejected`; a tela exibiu heap, pico, falhas e
novo limite. Margem acima da RAM livre e valor negativo lançaram RangeError e preservaram
o limite anterior. [Captura inspecionada](pcsx2-budget.png).

Builds Docker debug/release passaram em [build.log](build.log). O teste i386
ASan/UBSan do allocator passou em [host-i386.log](host-i386.log). Os hashes
estão em [results.json](results.json).

A API só altera a política do QuickJS do runtime atual; não reserva páginas
físicas nem impede que outros subsistemas usem a margem depois da chamada.
Pressão simultânea entre JS e assets continua pendente no roadmap.
