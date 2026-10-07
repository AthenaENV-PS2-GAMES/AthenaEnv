# Regressões gráficas e baseline 3D

Incremento de 04/10/2026. O usuário confirmou o recorte em PCSX2 com imagem
correta e log `input=3 output=5 clipped=3 rejected=0 culledObjects=0`.
O usuário também confirmou `athena_3d_regression_native.elf` com imagem correta
e enviou as nove linhas de baseline. O aceite nativo e a primeira medição
estão registrados abaixo. O usuário também enviou o baseline QuickJS e
confirmou imagem correta em todas as suas etapas. As regressões C/JS e
o baseline inicial no PCSX2 estão concluídos; PS2 real permanece pendente.

## Build e execução

```sh
node tools/modules.js configure --modules=screen,loop,render3d,draw,tilemap,system,timer,usbmass
```

Neste workspace, no PowerShell, use o SDK disponível pelo WSL:

```powershell
wsl -d Ubuntu -- docker run --rm --entrypoint /bin/sh -v /mnt/c/Users/User/ath:/src -w /src localhost/ps2swf-dev:m0 tools/build_3d.sh
```

O script compila QuickJS e os cinco exemplos C (cubos, recorte, regressões,
iluminação e texturas) e executa a suíte host.
Para outro checkout ou imagem, ajuste volume e imagem; dentro do container
o comando equivalente é `sh tools/build_3d.sh`.

A suíte QuickJS com ASan/UBSan usa a imagem local de 32 bits:

```powershell
wsl -d Ubuntu -- docker run --rm --entrypoint /bin/sh -v /mnt/c/Users/User/ath:/src -w /src localhost/ps2swf-host32:m0 tests/js/run.sh
```

- C: executar `bin/athena_3d_regression_native.elf`.
- JavaScript: executar `bin/athena_3d_js.elf --cfg=3d_regression.ini`, com
  `3d_regression.ini` e `3d_regression.js` na pasta dos demais scripts.
  Para launchers sem argumentos, usar uma pasta própria com
  `athena.ini` contendo `default_script=3d_regression.js`.

As cenas não precisam de assets externos. Cada etapa dura 180 frames,
com 60 de aquecimento e 120 de medição: 1.620 frames no total, cerca de
27 segundos a 60 FPS. Frames lentos prolongam a duração. O quadrado laranja
no topo identifica a etapa de 0 a 8 e a barra branca inferior mostra seu
progresso. A cena termina sozinha e imprime `3D regression complete`.

## Imagem esperada

| Etapa | Nome no log | Critério visual |
| --- | --- | --- |
| 0 | `depth` | Verde cobre exatamente o vermelho; a ordem de submissão alterna a cada 30 frames sem mudar a imagem. |
| 1 | `cull-back` | Apenas um dos triângulos com orientações opostas permanece. Registrar qual cor aparece. |
| 2 | `cull-front` | Apenas o outro triângulo permanece; cores visíveis nas etapas 1 e 2 são complementares. |
| 3 | `camera-tile-camera` | Amarelo à esquerda e ciano à direita. A segunda câmera desloca a projeção do ciano para a esquerda. O TileMap laranja no topo permanece correto após 3D → 2D VU1 → 3D. |
| 4 | `recreate` | Imagem verde sem corrupção ao descartar instâncias antes do flip e recriá-las a cada 30 frames. |
| 5 | `inside-batch` | Grade de 64 triângulos pequenos, uma malha compartilhada e Batch nativo. |
| 6 | `clip-batch` | Coleção de 64 instâncias com um vértice por triângulo cruzando o plano próximo; parte visível preservada. |
| 7 | `inside-individual` | Imagem e contadores de geometria iguais à etapa 5, desenhados individualmente. |
| 8 | `clip-individual` | Imagem e contadores de geometria iguais à etapa 6, desenhados individualmente. |
| 9 | `closed-cull` | Cubo fechado girando com `CULL_BACK`, uma cor por face: cada face visível aparece inteira e de uma cor. Acrescentada em 06/10/2026 depois da correção do culling nos programas VU1 ([3D.md](3D.md#backface-culling-em-malhas-corrigido-em-06102026)); as baselines das etapas 0–8 continuam comparáveis. |
| 10 | `edge-batch` | 64 triângulos laranja metade dentro, metade fora das bordas esquerda e direita: cortados retos na borda da tela, sem recorte em C (`guardBandObjectsPerFrame` 64, `clippedTrianglesPerFrame` 0). Acrescentada em 06/10/2026 com o guard band ([3D.md](3D.md#guard-band-06102026)). |

Quadrados e barra 2D permanecem visíveis em todas as etapas, verificando
restauração do teste de profundidade. Face culling ocorre no VU1: contadores
de triângulos submetidos das etapas 1 e 2 podem ser iguais apesar das imagens
diferentes. A etapa 3 usa o mesmo render target sem limpar entre câmeras;
não é uma API de viewport dividido.

## Coleta e interpretação

Cada etapa imprime `3D_BASELINE` seguido por JSON com média, p95 e p99 de
`drawTicks`, médias por frame dos contadores do renderer e memória.
Percentis usam nearest-rank sobre 120 amostras. C informa `CLOCKS_PER_SEC`
no início; JS usa as mesmas unidades nativas por `Timer.getTime()`.
Converta para milissegundos com o valor do build C equivalente:
`ticks * 1000 / CLOCKS_PER_SEC`.

A janela inclui draw e, na etapa 3, o TileMap intermediário. No JS também
inclui criação e soma dos objetos de estatísticas do binding. Exclui
criação/descarte de recursos, HUD, clear, flip e logs. Mede tempo decorrido
na submissão, incluindo esperas DMA dentro do renderer; não mede utilização
exclusiva da CPU ou conclusão da rasterização GS. Aquecimento reduz uploads
iniciais, mas não elimina interferência de GC ou do emulador.

`geometryBytes` mede posições e cores realmente copiadas para chunks DMA,
com padding até múltiplos de quatro vértices. O caminho contido usa 16 bytes
por vértice (xyz + cor); o recortado usa 20 (xyzw + cor). Tags DMA/VIF,
constantes, estado GS, uploads, ring DMA e 2D não entram. Essa métrica não
é o tráfego DMA total do frame. O contador C é de 64 bits.

Compare Batch/individual dentro de cada runtime. No JS, a diferença inclui
64 travessias e objetos de estatísticas versus uma chamada Batch. No C,
ambos atravessam o renderer por instância. Comparar contido e recortado
exige considerar também a diferença na quantidade de vértices enviada.

Registre revisão, versão/configuração do PCSX2 ou PS2, modo de vídeo,
runtime e módulos junto com o log. Execute pelo menos três vezes e preserve
as nove linhas de cada execução. Memória é um snapshot de alocações no
relatório, sem representar pico de carga ou VRAM; JS inclui wrappers
aguardando GC. O heap JS torna incomparáveis os valores absolutos dos
runtimes. Uma execução estabelece uma referência inicial; ganhos entre
versões exigem repetições e configuração comparável.

## Baseline nativo recebido em 04/10/2026

Log fornecido pelo usuário para `athena_3d_regression_native.elf`, com
confirmação visual da cena completa. Ambiente PCSX2 conforme contexto da
sessão; versão/configuração do emulador não informadas. Uma execução, com
120 amostras por etapa e `CLOCKS_PER_SEC=1000000`: um tick equivale a 1 µs.
Os valores originais estão em
[3d-native-2026-10-04.json](benchmarks/3d-native-2026-10-04.json).

| Etapa | Média de submissão (ms) | p95 (ms) | p99 (ms) | Geometria DMA/frame (bytes) | `allocs` (bytes) |
| --- | ---: | ---: | ---: | ---: | ---: |
| `depth` | 0,100450 | 0,101 | 0,101 | 128 | 1.512 |
| `cull-back` | 0,100317 | 0,101 | 0,101 | 128 | 1.512 |
| `cull-front` | 0,100325 | 0,101 | 0,101 | 128 | 1.512 |
| `camera-tile-camera` | 0,109442 | 0,110 | 0,111 | 128 | 1.540 |
| `recreate` | 0,101433 | 0,101 | 0,130 | 128 | 868 |
| `inside-batch` | 3,135983 | 3,138 | 3,148 | 4.096 | 11.312 |
| `clip-batch` | 18,342158 | 18,355 | 18,356 | 10.240 | 11.312 |
| `inside-individual` | 3,134100 | 3,136 | 3,144 | 4.096 | 11.312 |
| `clip-individual` | 18,341900 | 18,352 | 18,356 | 10.240 | 11.312 |

Contadores coerentes em todas as etapas: nenhuma rejeição de triângulo ou
culling de objeto. Os dois caminhos contidos submetem 64 triângulos e os
dois recortados submetem 128 a partir de 64, todos parcialmente recortados.
Cada coleção emite 64 passes/chunks. Batch e individual têm contadores iguais.

O Batch nativo não apresentou ganho relevante nesta execução: ele percorre
o mesmo kernel por instância, mantendo os 64 passes. A economia de travessias
JS→C foi avaliada com o log QuickJS na seção seguinte.

Neste cenário, o recorte elevou o tempo de submissão em aproximadamente
5,85 vezes e o payload em 2,5 vezes. Só a submissão de 18,342 ms supera
o orçamento de 16,667 ms para um frame a 60 FPS. Isso orienta a investigação
do recorte CPU e das barreiras/uploads por instância, mas não isola o custo
de double, transformação, triangulação, cópia ou espera DMA. Os números
do emulador não estabelecem o desempenho no PS2 real.

As quatro coleções mantêm o mesmo snapshot de `allocs`. Na etapa `recreate`,
o snapshot é menor porque as instâncias são liberadas antes do relatório.
Esses dados não medem pico de memória nem constituem um teste longo de vazamento.

## Baseline QuickJS e comparação recebidos em 04/10/2026

O usuário enviou as nove etapas e confirmou que todas as imagens aparecem
corretas. Log NTSC, com 120 amostras por etapa, sem erros reportados e com
marcador de conclusão. A conversão abaixo usa `CLOCKS_PER_SEC=1000000` do
build nativo equivalente da sessão; o log JS não imprime a frequência.
Os valores originais, incluindo todos os snapshots de memória, estão em
[3d-quickjs-2026-10-04.json](benchmarks/3d-quickjs-2026-10-04.json).

| Etapa | Média de submissão (ms) | p95 (ms) | p99 (ms) | Geometria DMA/frame (bytes) |
| --- | ---: | ---: | ---: | ---: |
| `depth` | 0,762683 | 0,765 | 0,768 | 128 |
| `cull-back` | 0,744292 | 0,749 | 0,750 | 128 |
| `cull-front` | 0,747375 | 0,750 | 0,751 | 128 |
| `camera-tile-camera` | 0,759083 | 0,763 | 0,764 | 128 |
| `recreate` | 0,765042 | 0,770 | 0,790 | 128 |
| `inside-batch` | 3,566742 | 3,569 | 3,578 | 4.096 |
| `clip-batch` | 18,788533 | 18,802 | 18,804 | 10.240 |
| `inside-individual` | 16,948992 | 17,010 | 17,033 | 4.096 |
| `clip-individual` | 32,130617 | 32,236 | 32,270 | 10.240 |

Todos os contadores de geometria coincidem com o nativo, etapa por etapa.
As coleções contidas enviam 64 triângulos, as recortadas 128 a partir de 64,
e ambas mantêm 64 passes/chunks. A redução de tempo do Batch não vem de
redução do payload ou de fusão de draws no GS.

| Coleção de 64 instâncias JS | Individual (ms) | Batch (ms) | Diferença (ms) | Redução nesta execução |
| --- | ---: | ---: | ---: | ---: |
| Contida | 16,948992 | 3,566742 | 13,382250 | 78,96% |
| Recortada | 32,130617 | 18,788533 | 13,342083 | 41,52% |

Essa comparação inclui chamadas JS→C, criação dos objetos de estatísticas
e sua agregação no script. O teste não separa cada parcela desses custos.
O Batch JS fica aproximadamente 0,431 ms acima do Batch C no caso contido
e 0,446 ms no recortado. A diferença de aproximadamente 13,3 ms entre as
formas de submissão JS orienta usar Batch para coleções; os percentuais
descrevem uma execução desse cenário no PCSX2, sem comparação com o legado
ou entre versões da migração.

O recorte continua ultrapassando o orçamento de 60 FPS mesmo com Batch JS,
com média de 18,789 ms apenas na submissão. Otimizações desse caminho exigem
profiling nativo e comparação com os contadores e imagens já aceitos.
O snapshot de memória das etapas 6–8 se mantém em `allocs=172988`,
`jsHeap=161076` e `jsObjects=515`; isso não substitui um teste longo de GC
ou vazamentos.

## Estado do aceite

| Verificação | Estado |
| --- | --- |
| Recorte anterior em PCSX2 | Confirmado pelo usuário em 04/10/2026. |
| Payload, retenção e teardown/reload do shader | Testes host de pacotes de produção passaram. |
| Builds EE native e QuickJS | Executados neste incremento. |
| Bindings QuickJS sob ASan/UBSan | Suíte de 32 bits passou; 62 verificações 3D em cada um de dois runtimes novos. |
| Imagens nativas de depth/culling/câmeras/TileMap/recriação | Confirmadas pelo usuário em PCSX2 em 04/10/2026. |
| Baseline nativo em PCSX2 | Uma execução recebida, com as nove etapas completas. |
| Imagens QuickJS de todas as nove etapas em PCSX2 | Confirmadas pelo usuário em 04/10/2026. |
| Baseline QuickJS em PCSX2 | Uma execução recebida; contadores coincidem com o nativo. |
| Regressões C/JS e baseline inicial no PCSX2 | **Concluídos por confirmação do usuário.** |
| Repetições e metadados completos do benchmark | Acompanhamento futuro para ampliar a comparação; aceite inicial concluído. |
| Imagens e sincronização em PS2 real | Pendente. |

Os testes host verificam pacotes e contadores, mas não executam shaders ou
rasterização. O aceite C/JS e os baselines iniciais estão registrados.
O próximo corte estático é
materiais/normais/UVs, retenção de texturas e Lights com passe difuso.
