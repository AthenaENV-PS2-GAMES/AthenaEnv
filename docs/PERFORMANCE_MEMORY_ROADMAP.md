# Performance, RAM, developer experience e segurança de memória

Análise e primeira implementação: 2026-10-07. A análise considera o código
atual de AthenaEnv; hipóteses de FPS precisam de medição no PS2/PCSX2.

## Primeira etapa implementada

| Área | Problema | Comportamento implementado |
| --- | --- | --- |
| BMP | Cabeçalhos, paletas, offsets e tamanhos não validados; cópia ignorava padding; OOM podia causar acesso inválido. | BI_RGB de 4/8/16/24 bits com dimensões até 1024, bounds antes de alocar, validação de paleta/offset/tamanho, checagem de cada leitura e alocação, orientação bottom-up/top-down e linhas alinhadas a quatro bytes. |
| PNG | Buffer temporário do tamanho da imagem e cópia integral; cleanup com variáveis locais modificadas após `setjmp`. | Decodificação direta no buffer final, inclusive Adam7, estado de cleanup no heap, checagem de layout, conversão de alpha/nibbles no próprio buffer e CLUT em ordem CSM1. |
| DMA | Reservas maiores que uma metade do ring eram aceitas; capacidade era checada apenas no mesmo canal. | Validação de canal, inicialização e tamanho por subtração sem overflow; capacidade verificada também depois de trocar canal; reserva da tag END. |
| Fonte bitmap | Todo o texto era serializado em um único pacote, ultrapassando o ring em textos longos. | Lotes dimensionados pela capacidade real do ring, mantendo glyphs, quebras de linha, culling e caminho de câmera rotacionada. |
| Build/DX | Objetos de outro conjunto de módulos ou flags podiam ser reutilizados. | Stamp por configuração com compilador, versão/target, flags C/assembly, includes e módulos; objetos recompilados somente quando os valores efetivos mudam. |
| Runtime | Subtração unsigned da reserva de stack e da memória restante podia sofrer underflow. | Erro explícito antes de criar o runtime se a stack não superar a reserva nativa de 24 KiB ou se não houver memória estimada disponível. |

Fontes principais: `src/modules/graphics/native/image_loaders.c`,
`src/modules/graphics/native/owl_packet.c`,
`src/modules/font/native/image_font.c`, `src/runtime/quickjs/ath_env.c` e `Makefile`.

A interface dos writers DMA pressupõe que `owl_query_packet` sempre retorna
um pacote válido. Uma reserva inválida agora produz diagnóstico e `abort()`
antes de escrever. Esse comportamento evita corrupção silenciosa, mas outros
chamadores com lotes variáveis ainda precisam ser auditados e divididos;
aumentar o ring não substitui essa correção.

Formatos BMP comprimidos, 32-bit/bitfields e 4-bit com largura ímpar são
rejeitados explicitamente. O cabeçalho DIB mantém alinhamento nativo e tem seu tamanho de 40 bytes verificado na compilação. No EE, marcar esse cabeçalho como `packed` gerava cargas LWL/LWR sem a extensão de sinal esperada pelo código gerado para alturas negativas; a regressão no PCSX2 identificou o problema que os testes x86 não mostravam. `ColorUsed=0` usa a paleta completa prevista pelo
formato. O decoder usa uma linha temporária, em vez de carregar todo o arquivo.

PNG conserva a política de paleta de 4 ou 8 bits; a largura de T4 deve ser par.
O último quadword dos buffers PNG/BMP é alocado e zerado, evitando leitura DMA além do buffer em texturas pequenas.

RGB com tRNS e grayscale com alpha são normalizados pelo libpng antes de
verificar o layout de destino. Erros deixam `Mem` e `Clut` nulos e fecham o
stream. JPEG foi auditado na continuação descrita abaixo.

### Economia esperada

Um PNG RGBA 1024×1024 deixa de exigir o buffer intermediário de 4 MiB.
O destino continua exigindo 4 MiB e a tabela de linhas ocupa aproximadamente
4 KiB no EE, além das estruturas internas do libpng. A economia de 4 MiB
vem da remoção de uma alocação identificável; não representa redução medida
de toda a RAM do runtime nem um ganho de FPS já demonstrado.

No BMP, o buffer temporário passa de toda a região de pixels do arquivo para
uma linha alinhada, com no máximo 3072 bytes nos formatos aceitos.

## Continuação: auditoria JPEG (2026-10-07)

O decoder mantém o estado libjpeg e o contexto de erro no heap, garantindo
cleanup definido após `longjmp`, inclusive quando a criação do decoder falha.
Dimensões de saída são limitadas a 1024×1024 antes de iniciar a descompressão;
isso limita o produto RGB a 3 MiB e evita overflow no cálculo do buffer.
O buffer final é alinhado a 128 bytes, arredondado a quadwords e zerado para
que o último DMA não leia além da alocação.

Grayscale é expandido para RGB pelo libjpeg. RGB/YCbCr usam CT24; CMYK/YCCK
são rejeitados até existir uma política explícita de conversão. Avisos libjpeg
são tratados como falhas: streams truncados não retornam pixels parciais como
assets válidos. Erros fecham o stream e deixam `Mem`/`Clut` nulos.

`scale_down` usa os fatores tradicionais 1, 2, 4 e 8, sem ampliar imagens
pequenas, e valida as dimensões calculadas; imagens que ainda excedem o limite
com 1/8 são rejeitadas. Não há promessa de redução arbitrária.

`sh tests/host/run_images.sh` passou com ASan, UBSan e LeakSanitizer: RGB,
grayscale, rejeição de CMYK, 1×1 com padding zerado, limite de dimensões,
escala, SOF extremo, truncamento de cabeçalho/dados/EOI e OOM no estado e
buffer final. O fault injection cobre as alocações da aplicação, não as
alocações internas da libjpeg compartilhada. A suíte também preserva as
regressões PNG/BMP. No sandbox, LeakSanitizer falhou devido a ptrace; a mesma
suíte passou fora dele. Builds Docker com DEBUG=1 e release passaram; a cena JPEG no PCSX2 2.8.2
validou RGB, grayscale, textura 1×1, rejeições e fonte bitmap em ambos os ELFs.
Capturas, hashes e limites: [validação JPEG](validation/jpeg-2026-10-07/README.md).
Não há medição comparativa de FPS ou RAM total desta etapa.

## Continuação: writers DMA e programas VU (2026-10-07)

`page_clear` agora emite lotes com no máximo `half_size - 12` páginas:
11 quadwords de comandos mais END. Cada lote restaura TEST/XYOFFSET antes
do próximo draw e conserva a ordem por coluna das páginas. Um framebuffer
1920×1080 tem 1020 páginas, portanto usa lotes de 1012 e 8 no ring mínimo,
em vez de abortar com uma reserva de 1031 quadwords. O writer portátil fica
em `src/modules/graphics/native/page_clear.h`, usado também na regressão host.

O gerenciador MPG reserva exatamente uma tag REF por comando de até 256
instruções, sem deixar quadwords não inicializados entre uploads. Tamanhos
de buffer são palavras de 32 bits; arquivos são medidos em bytes e convertidos
explicitamente. São rejeitados programas vazios, desalinhados, sem quadwords
completos ou maiores que a memória VU. Os limites são 512 instruções no VU0
e 2048 no VU1 (4/16 KiB), não 256/1024 instruções. O endereço da cópia direta
VU0 usa oito bytes por instrução; a cópia direta VU1 é rejeitada.

O cache procura entradas mesmo depois de slots vazios e reinicia quando
faltam slots ou espaço de código. Isso evita acessar `entries[-1]` ou reutilizar
um endereço que sobrepõe programas vivos; pode aumentar reuploads em cargas
que esgotam o cache, sem benchmark comparativo nesta etapa. O código fonte
recebe SyncDCache antes do REF; `vu_mpg_unload` espera a geração DMA que o
referencia antes de liberar buffers, inclusive uploads ainda no ring.

A auditoria encontrou lotes existentes nas listas 2D e na fonte atlas. Render3D
limita os lotes a 48 vértices, morph a 33 com até quatro targets, skin a 30 e
paleta a 24 joints; os pacotes resultantes cabem na metade de 1024 quadwords.
TileMap usa REF por lote de até 50 sprites (36 rotacionados); partículas usam
até 44 registros por lote (92 quadwords). O pacote de inicialização GIF é
limitado pelos 18/19 registradores de estado. Não foram encontrados outros
writers de tamanho variável sem esses limites. As regressões diretas das
listas 2D e fontes atlas foram acrescentadas na etapa abaixo.

Os novos testes verificam canários, ordem e restauração dos registradores,
fronteiras MPG de 256 instruções, memória VU cheia, cache com holes/17 slots,
tamanhos de arquivo, alinhamento e vida útil REF. A suíte de segurança passou
com ASan/UBSan/LeakSanitizer; a suíte host completa também passou. Builds
Docker DEBUG=1/release e cenas PCSX2 de sprites/fonte e clear HD passaram.
Evidências e limites: [validação DMA](validation/dma-writers-2026-10-07/README.md).

## Regressões: listas 2D e fonte atlas (2026-10-07)

`tests/host/run_draw.sh` compila `owl_draw.c`, o allocator DMA real e o renderer
atlas de `fntsys.c`. O harness decodifica os comandos VIF DIRECT e GIF e
verifica se cada writer escreveu exatamente a reserva feita, se todos os
vértices mantêm ordem/posição/UV/cor e se os canários do ring continuam íntegros.
Somente os helpers de packing EE, a camada DMA/GS e o bind de textura são
substituídos no host; o batching e a montagem dos pacotes são os de produção.
Os layouts de fonte são injetados, sem depender da rasterização FreeType.

Foram acrescentados 1458 casos para os nove writers (pontos, linhas simples e
Gouraud, triângulos simples e Gouraud, triângulos texturizados simples e
Gouraud, sprites e retângulos texturizados), e 810 para a fonte atlas:
rings 2048/4096/8192; capacidade -1/exata/+1 e múltiplos lotes; ring parcialmente
ocupado; câmera identity/axis/rotated; textura pendente, já residente e transição
entre ambas; falha de bind e listas inválidas. A fonte cobre 1/2/5 passes,
1/2 atlases, 2300 glyphs, ordem por pass, culling, outline, sombra e rotação
pelo writer real de retângulos.

A suíte de segurança executa esses testes automaticamente. Os testes de listas
não exigem bibliotecas extras; fonte atlas precisa de headers FreeType e
`pkg-config` (skip explícito se faltarem). A imagem Docker i386 instala ambos,
e a suíte JS agora executa a suíte de segurança com ASan/UBSan antes dos demais
testes. Fixtures usam matrizes exatas em float32 para não atribuir ao writer
as diferenças de precisão intermediária do x87; comparações continuam exatas.

Execuções, builds Docker e screenshots PCSX2 ficam em
[validação das regressões](validation/regressions-2026-10-07/README.md).
A cena `tests/draw_lists_regression_visual.js` exercita 1920 sprites de Image
e 1344 glyphs de fonte atlas com outline e câmera rotacionada. As mudanças
desta etapa são testes, stubs e integração da suíte; nenhum ajuste adicional
nos renderers foi necessário.

## Continuação: contador de memória e locks do SDK (2026-10-07)

`allocs_size` agora atualiza e lê sob uma seção crítica curta com `DIntr`,
restaurando interrupções somente se estavam habilitadas. malloc/calloc/realloc,
memalign/free e a consulta de tamanho do bloco permanecem fora dessa seção;
nenhum semáforo ou allocator é chamado com interrupções mascaradas por ela.
`get_used_memory` usa o mesmo snapshot protegido. O contador registra bytes
úteis do allocator, não overhead, fragmentação, stacks de workers ou VRAM.

A libc efetivamente ligada nesta imagem usa `__malloc_lock`/`__malloc_unlock`
e o mutex recursivo da libcglue com WaitSema. O disassembly confirma aquisição
do semáforo antes de atualizar dono/recursão; não foi acrescentado um segundo
lock ao heap. Essas chamadas continuam restritas a threads, nunca handlers
de interrupção. A conclusão vale para esta toolchain, não para todo PS2SDK.

Regressões x86_64/i386 com ASan/UBSan passaram: oito workers × 20000 ciclos,
total exato de blocos retidos e retorno a zero, realloc grow/shrink/zero/OOM,
alocações falhas e preservação de interrupções já desabilitadas no getter.
Um ELF nativo exercitou quatro workers × 10000 ciclos no PCSX2, com alternância
explícita após malloc/realloc; total inicial/final de 420 bytes e sem deadlock.
Builds Docker debug/release, suíte de segurança e cena release passaram.
Evidências: [validação do contador](validation/memory-accounting-2026-10-07/README.md).

## Continuação: pico e falhas de alocação (2026-10-07)

`System.getMemoryStats()` agora expõe `allocsPeak` e `allocationFailures`.
O pico acompanha o máximo de bytes úteis vivos no wrapper nativo desde o
startup; falhas contam pedidos não nulos de malloc/calloc/memalign e realloc
com tamanho positivo. `realloc(p, 0)` e pedidos de tamanho zero não contam
como falha. A tela `bin/tests/memory_stats.js` mostra ambos; o teste de API
confere tipo e coerência do pico.

A regressão do wrapper força OOM e realloc grow/shrink/free e confere pico e
incremento exato de quatro falhas. Build Docker debug/release passou.

## Contabilidade do allocator (2026-10-07)

`System.getMemoryStats()` também expõe arena reservada (`heapReserved`), chunks
ocupados pelo allocator (`heapAllocated`), livres (`heapFree`), contagem de
chunks livres e o espaço livre do topo (`heapTopFree`). `heapOverhead` estima a
diferença entre chunks ocupados e bytes úteis do wrapper; `heapNonTopFree` é o
total livre fora do topo e serve como sinal de fragmentação. Esses valores vêm
de `mallinfo` do newlib do EE.

São snapshots globais, não uma contabilidade exata por módulo. O allocator pode
mudar entre a leitura de `mallinfo` e dos contadores do wrapper quando workers
estão ativos. `heapOverhead` também inclui metadados/alinhamento do allocator e
é uma estimativa; `heapNonTopFree` não afirma que cada chunk seja inutilizável.
Atribuição persistente por subsistema continua pendente: exige marcar a
propriedade do bloco até o `free` ou alocadores por domínio. Stacks, QuickJS e
VRAM seguem apresentados em métricas separadas já existentes.

O teste host valida o mapeamento dos campos sintéticos do `mallinfo`; a cena
visual valida a exposição dos dados reais do SDK no PCSX2. Builds Docker debug
e release também verificam o vínculo contra a newlib do EE. [Evidências](validation/heap-breakdown-2026-10-07/README.md).

## Scratch de skinning e morph (2026-10-07)

Scene3D agora oferece `Scene3D.trimScratch()` para liberar buffers globais
CPU de posições/normais usados no fallback de skinning e morph após uma
transição de cena. A próxima deformação volta a alocá-los sob demanda. O
shutdown nativo do módulo chama a mesma rotina, e chamadas repetidas são
seguras. A API exige chamada no thread principal entre draws. Os buffers CPU
são serializados em pacotes durante `draw`; o caminho que retém DMA_REF usa
os dados originais do mesh e não referencia esse scratch.

O harness host executou skinning/morph em malha indexada e expandida antes e
depois de liberar o scratch, com os mesmos vértices, normais e bounds; também
chamou trim duas vezes. `run_3d.sh` passou e os dois fontes EE/QuickJS do módulo
foram compilados pela toolchain Docker. Scene3D é opcional e não faz parte da
configuração de módulos atualmente ativa, então a mudança não altera o ELF
padrão. [Validação do scratch](validation/scene3d-scratch-2026-10-07/README.md).

## Orçamento QuickJS e margem nativa (2026-10-07)

`System.setNativeMemoryHeadroom(bytes)` recalcula o limite absoluto do heap
QuickJS a partir do uso JS atual e da RAM EE livre observada, deixando a
quantidade pedida para alocações nativas da próxima fase. O limite inicial
continua em metade da RAM livre no começo do runtime. A API rejeita margens
que deixariam menos de 64 KiB para QuickJS e retorna o novo teto; ele aparece
em `System.getMemoryStats().jsLimit`. Isso permite baixar o teto JS antes de
carregar uma cena ou asset pesado, sem recompilar o runtime.

A margem é um cálculo de snapshot, não uma reserva protegida pelo allocator:
workers e alocações nativas posteriores podem consumi-la. O jogo deve chamar
a API em transições de fase e atualizar a política quando a carga nativa mudar.
Ainda falta pressão concorrente JS+assets para definir um limite automático
seguro. PCSX2 confirmou o recálculo, a coerência com `jsLimit` e a rejeição
de uma margem maior que a RAM livre. Builds Docker debug/release passaram.
[Validação](validation/memory-budget-2026-10-07/README.md).

## Próximas etapas

| Prioridade | Trabalho pendente | Critério de conclusão |
| --- | --- | --- |
| Alta | Validar `scale_down` JPEG no EE e executar em PS2 físico. | PCSX2 já confirmou RGB/grayscale e textura 1×1 em DEBUG=1/release; falta escala interna libjpeg no EE e hardware real. |
| Média | Atribuir memória viva por subsistema/domínio. | Preservar o dono desde a alocação até o free, inclusive realloc; complementar os snapshots globais de `mallinfo` e as métricas existentes de stack/VRAM. Overhead e sinal de fragmentação agora são expostos. |
| Média | Fechar o orçamento combinado automático de QuickJS e assets nativos. | Pressão concorrente JS+assets e ajuste que preserve margem também após crescimento nativo; API atual calcula headroom por snapshot. |
| Média | Medir CPU, uploads, espera DMA, GC e pior tempo de frame. | Cenas reproduzíveis com distribuição de frame times e pico de RAM; comparar ring 2048/4096 e DMA_REF sem atribuir stall a simples contagem de waits pendentes. |
| Baixa | Corrigir/remover `str_split` e tratar erro negativo de `vsnprintf` em `s_sprintf`. | Delimitadores repetidos, string vazia e OOM têm semântica definida; eliminar `strtok` global e comparação de ponteiros inválida. Não foram encontrados chamadores atuais de `str_split`. |

Manter as otimizações existentes: seleção de módulos, runtime nativo,
`--gc-sections`, geometry DMA_REF, indexed meshes, batching, dirty propagation
e culling de subárvores. Mudanças adicionais precisam preservar a vida útil dos
buffers até a conclusão das leituras DMA.

## Testes e reprodução

```sh
# Sem dependências de PS2; fonte atlas requer FreeType headers/pkg-config.
sh tests/host/run_safety.sh
sh tests/host/run.sh

# libpng e libjpeg do host; sanitizadores e fault injection.
sh tests/host/run_images.sh

# Toolchain Docker; ajustar UID/GID ao usuário do workspace.
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  -e ATHENAENV_SRC=/src/src athenaenv-build:latest \
  make -j4 DEBUG=1 EE_BIN_PREF=athena_safety all

# Imagem i386 necessária ao QuickJS modificado.
docker build -f Dockerfile.jstests -t athenaenv-js-tests:safety .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  athenaenv-js-tests:safety sh tests/js/run.sh
```

`run_images.sh` cobre PNG RGB/RGBA/grayscale-alpha, Adam7, paletas T4/T8,
BMP com padding/top-down, paleta padrão, RGB555, cabeçalhos malformados,
truncamento e OOM em pixels/paleta/metadados/linha. ASan, UBSan e LeakSanitizer
validam o código de decoder real, com libpng/libjpeg do host.
`run_safety.sh` valida reservas exatas, canal inválido, SIZE_MAX e canários do
ring; o renderer bitmap real desenha 505, 506 e 2300 glyphs com rings
2048/4096/8192, com e sem marcador de upload, verificando a ordem e posição
entre lotes, quebras de linha, culling e rotação.

A imagem de testes JS instala as bibliotecas dos novos testes. A suíte JS
executa os testes dos decoders antes das regressões 3D e dos bindings.
A suíte host padrão executa os testes DMA/fontes sem depender de libpng.
`sh tools/verify_build_config.sh`, dentro da toolchain, verifica timestamps
de objetos com flags idênticas, mudança para `-O1` e restauração.

A cena `bin/tests/memory_safety_visual.js` carrega seis fixtures reais, rejeita
dois arquivos truncados e desenha 1275 glyphs em 17 linhas. Para gerar os assets
novamente: `python3 tools/make_memory_safety_assets.py`. Execute o ELF com
`--ignorecfg --script=tests/memory_safety_visual.js`; no PCSX2 isso corresponde
a `-gameargs '--ignorecfg --script=tests/memory_safety_visual.js'`. A configuração
`bin/athena.ini` não precisa ser alterada.

Resultados e limitações da validação desta etapa ficam em
`docs/validation/safety-2026-10-07/README.md`.
