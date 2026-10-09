# Melhorias do ambiente 3D

Levantamento de performance, compatibilidade, seguranca de memoria e
developer experience dos modulos 3D (render3d, model3d, camera3d, lights,
scene3d, gltf3d, animation3d e, de forma mais superficial, physics3d,
collision3d, particles3d, tween3d e camerarig3d). A rodada 14 estende
o levantamento aos sistemas lod, audio3d, triggers3d, nav, sky e Bench.

Legenda de status:

- **FEITO**: implementado e coberto pelos testes host C/JS (`tests/host/run_3d.sh` e/ou `tests/js/run.sh`).
- **PARCIAL**: etapa implementada e validada; restante explicitado no ponto.
- **PENDENTE**: ainda nao implementado; motivo e caminho sugerido descritos.
- **VALIDAR NO HW**: implementado, mas o efeito final (GS/VU1) so pode ser
  confirmado no console ou no PCSX2; os testes host cobrem pacotes e CPU.

## 1. Seguranca de memoria

| # | Problema | Local | Status |
|---|---|---|---|
| M1 | Ciclo de referencia skin -> joints: um joint ancestral do no skinned impede que a subarvore seja liberada (vazamento de malhas e texturas). | `scene3d.c` (`athena_skin3d_*`) | FEITO: o skin guarda os joints como referencias fracas; um joint liberado zera a entrada e o draw devolve `ESTALE`. |
| M2 | Structs com membros `aligned(16)` alocadas com `calloc` (lights, tween3d). No R5900 `lq`/`sq` ignoram os 4 bits baixos: copia de struct num bloco alinhado em 8 le dados deslocados. | `lights.c`, `tween3d.c` | FEITO: `memalign(16)`. |
| M3 | Finalizer de `Texture` chama `graphics_wait_idle()` (flush + FINISH) em momento arbitrario do GC: engasgo no meio do frame. | `texture3d_gs.c` | FEITO: o release vai para uma fila; VRAM e pixels sao liberados sem espera depois de 2 flips com FINISH (`graphics_finished_frames`) ou de qualquer `graphics_wait_idle` (o proprio upload de outra textura). Em single buffering a fila so anda nesses waits. `athena_model3d_module_shutdown` drena tudo. Validado no PCSX2 (etapa `churn`). |
| M4 | Reuso de pass compara camera/textura por ponteiro (ABA possivel quando um objeto e liberado e outro alocado no mesmo endereco dentro de um grupo). | `render3d_gs.c` | FEITO: camera usa revisao global unica da view_projection (inclusive init e alteracoes por setters); textura imutavel usa identidade global unica. O pass guarda apenas as revisoes, sem ponteiros pendentes. Mudanca da mesma camera dentro de um group reenvia as constantes. Validado no PCSX2 (`camera-group`, 2 passes e dois quads separados). |

## 2. Compatibilidade

| # | Problema | Local | Status |
|---|---|---|---|
| C1 | UVs restritos a [0,1] e sem REPEAT. A GS faz REPEAT nativo (CLAMP.WMS/WMT=0) em texturas potencia de 2; os programas VU1 so calculam `ST = UV*Q`. | `model3d.c`, `render3d_clip.c`, `texture3d_gs.c` | FEITO: UVs finitos em [-16, 16] (limite de precisao do GS para texturas de ate 512); `Texture` ganhou `wrap` (`CLAMP`/`REPEAT`); o clipping interpola sem forcar [0,1]. Validado no PCSX2 (etapa `wrap`); PS2 real: aceite visual do usuario em 07/10/2026; sem tempos A/B. |
| C2 | glTF: sampler ausente (padrao REPEAT) ou REPEAT e recusado; filtros com mipmap e min != mag recusados. | `model3d_load.c` | FEITO: sem sampler = REPEAT + LINEAR; REPEAT e CLAMP aceitos; filtros com mipmap usam o nivel base; o filtro vem do mag (ou do min). MIRRORED_REPEAT continua recusado (a GS nao espelha). |
| C3 | Texturas paletizadas (PNG/BMP 4/8 bits) e CT16 recusadas pelo decoder 3D. | `texture3d_gs.c` | FEITO: copia canonica CT32 na CPU e armazenamento lossless T4/T8 + CLUT na VRAM quando menor; inclui alpha, CSM1 e fallback CT32 por falta de RAM/mais de 256 cores. PCSX2: T4 512x512 ocupa 131328 vs 1048576 bytes, oito texturas residentes; reset, alpha mask, VU1/CPU e churn validados. PS2 real: aceite visual do usuario em 07/10/2026; sem tempos A/B. |
| C4 | Sem cache de textura no loader: cada primitiva glTF recarrega e reenvia a mesma imagem. | `model3d_load.c`, `gltf3d.c` | FEITO: cache por imagem/sampler durante o load. |
| C5 | OBJ recusa quads/n-gons e `map_Kd`. | `model3d_load.c` | FEITO: triangulacao em leque; um unico `map_Kd` (REPEAT) por arquivo. Varias texturas diferentes continuam recusadas com mensagem. |
| C6 | `KHR_mesh_quantization` recusada, embora o `cgltf_accessor_read_float` ja converta. | `model3d_load.c`, `gltf3d.c` | FEITO. |
| C7 | Animacao de morph weights so atinge a 1a primitiva de malhas multi-primitiva. | `gltf3d.c` | FEITO: as primitivas extras seguem os pesos do no pai (`athena_node3d_set_morph_follower`). |
| C8 | Imagens embutidas em `.glb` (bufferView) e data URIs recusadas. | `model3d_load.c` | FEITO: `load_image_memory_ex` (graphics, via `fmemopen` do newlib), `athena_image_decode_memory`, `athena_texture3d_load_memory`; bufferView e data URI base64. Validado no PCSX2 (etapa `embedded`). |
| C9 | `alphaMode: MASK` recusado. | `model3d_load.c` | FEITO: material `alpha_mask`/`alpha_cutoff` (JS `alphaCutoff`); o pass liga TEST.ATE (GEQUAL, AREF = cutoff*128, AFAIL KEEP) e TEX0.TCC, e entra na chave de reuso. Texturas guardam o alpha real (escala GS); o decoder normaliza o alpha dos loaders (0..0x80) para 0..255. BLEND continua recusado. Validado no PCSX2 (etapas `alpha-mask` e `embedded`). |
| C10 | CUBICSPLINE convertido para LINEAR em silencio. | `gltf3d.c`, `animation3d.c` | FEITO: `ATHENA_ANIM3D_CUBIC` (JS `"cubic"` com `inTangents`/`outTangents`), Hermite com tangentes por segundo; rotacoes normalizadas depois. |

## 3. Performance

| # | Problema | Local | Status |
|---|---|---|---|
| P1 | Malhas nao indexadas (triangle soup): 3x trafego DMA/VU1, memoria e custo de skinning na CPU. | `model3d.c` | FEITO: streams compactos quando menores e lotes indexados VU1 para estatica, skin e morph, preservando ordem/clipping/flat normals. A/B PCSX2: RAM -43,8% a -51,8%, payload -51,8% a -58,4%; draw ate GS ociosa -40,8% skin / -26,0% morph. Rodada 13 reduz mais 3,1%-4,7% na submissao estatica; custo vs soup fica ~3%. PS2 real: aceite visual do usuario em 07/10/2026; sem tempos A/B. |
| P2 | Todo vertice e copiado (`memcpy`) para um ring de 16 KB por metade. Os streams da malha ja sao imutaveis, alinhados e com padding. | `render3d_gs.c` | FEITO: DMA_REF para streams imutaveis, retencao por geracao e fallback inline para dados temporarios/sem memoria na fila. Host executa os dois modos e le os enderecos REF apos descarte, com ASan/UBSan; reset, padding e crescimento da fila cobertos. A/B PCSX2: -20% a -28% no draw das nove cenas, contadores identicos. Pacote final aceito visualmente no PS2 real; A/B inline/REF no console nao medido. |
| P3 | JS nao consegue agrupar draws: cada `Render3D.draw()` paga barreira, programa, camera e restauracao de GS. | `ath_render3d.c` | FEITO: `Render3D.group(fn)`. |
| P4 | Luzes reenviadas a cada objeto (17-26 qw) mesmo sem mudanca. | `render3d_gs.c` | FEITO: reenvio so quando a revisao (global, unica por estado) muda dentro do pass; a matriz de normais continua por objeto. A/B no PCSX2 (`3d_profile`, so essa mudanca): -2,6% a -3,7% no draw das cenas iluminadas, 0% nas unlit. |
| P5 | Scene3D ordena so por pipeline e copia uma `MeshView` inteira por no por frame para isso. | `scene3d.c` | FEITO: chave (pipeline, textura) em cache no `set_mesh`; ordenacao estavel. |
| P6 | Personagens skinned texturizados nunca usam o VU1. | `scene3d.c`, `vu1/draw_3D_skinned.vcl` | FEITO: UV V2_32 + ST/Q com perspectiva, MODULATE, UNLIT/DIFFUSE e alpha mask; fallback CPU para clipping/mais de 24 joints. `.vsm` regenerado no Docker, cinco etapas no PCSX2 e suites C/JS com sanitizadores verdes. A/B PCSX2: draw -86,5% (1 coluna de 2334 vertices) / -86,8% (4 colunas); PS2 real: aceite visual do usuario em 07/10/2026; sem tempos A/B. |
| P7 | Primeiro bind de textura faz `graphics_wait_idle()` no meio do frame. | `texture3d_gs.c` | FEITO: `athena_texture3d_upload()` / `Texture.upload()`. |
| P8 | `double` no codigo de carga (emulado em software no R5900). | `model3d.c`, `lights.c` | FEITO: float com reescala pelo maior componente; validacao de triangulo degenerado e geracao de normais usam a mesma funcao (`face_normal`). |
| P9 | Buffer OWL de 2048 qw (16 KB por metade) causa esperas frequentes por DMA. | `graphics.c` | FEITO (diagnostico/A/B): tamanho configuravel, contadores opcionais de ocupacao/flush/esperas. PCSX2: 4096 elimina um flush por capacidade/frame, mas draw ate GS ocioso piora 1,4%-1,9%; padrao 2048 mantido. Diagnostico desligado no build normal. PS2 real pendente antes de mudar o padrao. |
| P10 | Custo fixo por objeto subiu ~1% no microbenchmark de objetos de 1 triangulo (`3d_regression`, unlit), sem mudanca de contadores. ~0,2% e a chave de alpha (C9) por draw; o resto nao foi isolado (a baseline de 06/10 nao corresponde exatamente ao codigo anterior a esta rodada). Cenas reais nao mostram o efeito (o P4 domina). | `render3d_gs.c` | PARCIAL: A/B da mesma revisao removeu a copia redundante de MeshView em draw_view; 64 triangulos: -2,56% batch / -1,90% individual, contadores iguais. Custo isolado e melhoria entregue; a regressao historica de ~1% nao pode ser atribuida sem a revisao exata da baseline de 06/10. |
| P11 | Constantes de escala RGB, textura, iluminacao e culling reenviadas por lote indexado. | `render3d_gs.c` | FEITO: envio uma vez por objeto, apos o fence do VU1 anterior. A/B PCSX2 da mesma revisao: submissao -4,65% unlit / -3,10% diffuse / -1,25% skin / -1,00% morph, contadores iguais. |

## 4. Developer experience

| # | Problema | Local | Status |
|---|---|---|---|
| D1 | Loaders devolvem so "Model feature is not supported (code -5)". | `model3d`, `gltf3d` | FEITO: `athena_model3d_detail()` com o recurso exato; o JS inclui detalhe e caminho. |
| D2 | Todo `-1` do render vira "Invalid or overflowing 3D transform". | `render3d` | FEITO: `athena_render3d_error_detail()` (normais ausentes, UV invalido, matriz nao finita etc.). |
| D3 | `setScale(0,0,0)` (esconder objeto) lanca excecao em malhas DIFFUSE. | `render3d_shade.c` | FEITO: escala nula conta como objeto descartado (culled). |
| D4 | Um player com erro aborta todos os outros e o sistema do Loop. | `animation3d.c` | FEITO: o player com erro e pausado (nao falha de novo a cada frame), os demais avancam no mesmo frame e o primeiro erro e devolvido no fim. |
| D5 | `node.getParent() !== node.getParent()` (um wrapper novo por acesso). | `ath_scene3d.c` | FEITO: cache fraco por runtime/no; root/getParent/getChild e wrappers do loader reutilizam o mesmo objeto JS vivo, preservando propriedades e subclasses. Dispose invalida aliases; finalizer/dispose removem a entrada antes do release nativo. Ciclos de GC e troca de wrapper validados em host JS e PCSX2. |
| D6 | `Mesh.fromGeometry` sem joints/weights/morph; `Instance` sem getters de TRS; `Mesh` sem bounds. | `ath_model3d.c` | FEITO: `Instance.getPosition/getRotation/getScale(out?)`, `Mesh.getBounds(out?)` e `Mesh.fromGeometry` com joints/weights/targetPositions/targetNormals. Streams copiados e expandidos juntos; subarray, getters, tipos e limites validados. Pesos finitos extremos normalizados sem transbordo. Morph e skin procedurais validados no PCSX2 (VU1 e CPU). |
| D7 | Limites silenciosos (65532 vertices apos expansao, 4+4 luzes, 24 joints no VU1, textura 512). | `.d.ts` | FEITO: `Model3D.MAX_JOINTS` (256), `MAX_TARGETS` (8), `MAX_VERTICES` e `UV_LIMIT` exportados; 4+4 luzes exportadas, limite VU1 de 24 joints e textura 512 documentados. |

## 5. Validacao

### Pendencias atuais (09/10/2026)

- Implementacoes M1-M4, C1-C10, P1-P9/P11 e D1-D7 entregues.
- PS2 real: usuario confirmou todos os exemplos do pacote visualmente
  iguais ao PCSX2 e com boa performance em 07/10/2026. Aceite visual
  registrado em [3D_PS2_VALIDATION.md](../docs/3D_PS2_VALIDATION.md).
  Avaliacao qualitativa; tempos/logs A/B nao fornecidos.
- P10: melhoria isolada entregue; atribuicao historica permanece sem
  conclusao. O usuario confirmou que nao possui a revisao/patch original.
  Nao reconstruir nem substituir essa baseline por uma revisao presumida.
- Fase 4 revisada e Bench entregue na rodada 14: testes host/PCSX2 e
  builds EE passaram; PS2 real pendente para estes novos modulos. A busca
  sincronizada do labirinto 128x128 custa ~66 ms no emulador: distribuir
  pedidos e limitar expansoes; retomada incremental ainda nao implementada.
- As rodadas abaixo registram o estado na data de cada experimento;
  mencoes antigas a implementacoes pendentes nao substituem este registro.


### Rodada 14 (09/10/2026): sistemas de jogo e medicao

- Revisao da Fase 4 do [roadmap](3D_DX_MODULES_ROADMAP.md#6-fase-4-sistemas-de-jogo-3d):
  LOD reexibe nos ao sair de bandas ocultas/limites globais; Sky preserva
  setTime antes da inicializacao; Triggers3D evita vazamento em opcoes
  invalidas, eventos para ids reutilizados e update recursivo. LOD/Nav/Audio3D
  revalidam ou retem recursos nativos quando getters JS podem chamar dispose.
- Nav reconstrui caminhos no heap de busca ja existente, removendo dois
  buffers temporarios por consulta. Instrumentacao C de malloc/calloc/realloc:
  **zero alocacoes em 100 buscas e moveTo**; a API JS ainda cria Float32Array
  para o resultado. Suavizacao preserva desvios por custo, rays quase axiais
  e coordenadas extremas passam nos testes; nearestWalkable busca apenas
  dentro da grade, inclusive para pontos externos e raios grandes.
- Bench inicia a Fase 5: tarefas sincronas em lotes por frame, warmup,
  amostras em buffer reutilizado, media/p95/min/max, cleanup, cancelamento e
  checkpoints JSON. Mede com ticks nativos do Profiler; setup/teardown e
  escrita ficam fora do intervalo. Draws medem CPU, salvo espera GS explicita.
- Suite completa C/JS i386 ASan/UBSan, incluindo DMA_REF=0/1 e teste Nav
  instrumentado. Builds EE QuickJS e native hello ligados em copia isolada.
  PCSX2 2.8.2: Fase 4 129 verificacoes, Profiler 50 e Bench 25, sem falhas.
  Bench no host tem 27 checks por runtime (dois runtimes); a diferenca vem
  dos testes de escrita com stub versus arquivo real no emulador.
- [Exemplo](../bin/world_systems_bench.js) rodou e gravou
  [resultado/configuracao/hashes](../docs/benchmarks/world-systems-bench-2026-10-09.json).
  MEDIDO em uma execucao no PCSX2, sem rendering nem espera pelo GS:
  LOD 300 grupos media/p95 0,1598/0,1724 ms; Nav labirinto 128x128
  66,2583/66,2716 ms; Triggers3D 50 zonas x 20 corpos 0,4146/0,4338 ms.
  Referencia vazia de 100 chamadas/lote: media 0,0060 ms/chamada.
- Nao houve A/B controlado da remocao de alocacoes, nem medicao em PS2 real
  dos novos sistemas. A atribuicao historica de P10 continua pendente com
  o mesmo limite de baseline registrado acima. Busca incremental de Nav,
  live reload, UI e CLI de assets sao os proximos trabalhos do roadmap.

### Rodada 13 (07/10/2026): constantes dos lotes indexados (P11)

- A rodada 12 identificou custo extra de submissao estatica. Cada lote
  repetia o qword 26 (escala RGB, textura, iluminacao e culling), embora
  todos os lotes do objeto usassem os mesmos valores. O renderer agora
  envia esse estado uma vez por objeto, depois do fence da invocacao VU1
  anterior. O formato/ordem dos streams e os microprogramas nao mudam.
- Reserva de pacote reduzida em 2 qwords por lote; um upload de 2 qwords
  permanece por objeto. Na grade do sample, poupa 62 qwords/992 bytes
  de constantes em estatica/skin e 126 qwords/2016 bytes em morph. Sao
  tags/constantes; geometryBytes e RAM da malha permanecem iguais.
- Testes verificam um upload por objeto em estatica, textura, skin e morph,
  alternancia UNLIT/DIFFUSE e culling no mesmo group, reservas de pacotes e
  DMA_REF apos liberar a malha. Suite C/JS i386 com ASan/UBSan verde;
  testes C finais nos modos inline/REF tambem passaram.
- A/B usa o mesmo sample native da rodada 12, mesma revisao/configuracao,
  com apenas essa mudanca. Tres pares sequenciais, 60 frames de warmup e
  120 amostras por etapa. Contadores identicos em todas as execucoes.
- Submissao: unlit 64,825 -> 61,811 us (-4,65%); diffuse 75,583 -> 73,239
  (-3,10%); skin 227,283 -> 224,447 (-1,25%); morph 531,742 -> 526,433
  (-1,00%). Draw ate GS ociosa cai 3,75%, 2,31%, 0,90% e 1,03%, respectivamente.
  Comparacao e incremental sobre a rodada 12; nao atribui a baseline
  historica de P10. Submissao estatica ainda custa ~3% mais que soup.
- QuickJS final: 11 etapas no PCSX2 concluidas; diffuse, skin texturizado,
  backface e point lights/fog inspecionados em capturas. ELFs native
  antes/depois e QuickJS atualizados em bin, com packed da versao final.
- Resultado, hashes e patch exato preservados em
  docs/benchmarks/3d-indexed-constants-p11-2026-10-07.json e .patch.
  PS2 real e atribuicao historica de P10 continuam pendentes.

### Rodada 12 (07/10/2026): armazenamento compacto e indices no VU1 (P1)

- A criacao da malha prepara streams imutaveis por lote, uint16 por entrada
  para CPU e indices locais uint8 para VIF S_8. Compara custo total com soup;
  sem economia, com normais geradas por face ou falha nas alocacoes opcionais,
  mantem o caminho anterior. Padding por lote garante alinhamento e bounds
  consideram somente vertices referenciados. Ordem dos triangulos preservada.
- VU1: estatica/skin usam ate 24 vertices/48 entradas; morph, 16/24, com ate
  quatro targets. Transformacao, iluminacao e STQ sao calculados por vertice
  e guardados no cache. Emissao mantem perspectiva, alpha, fog e culling.
  BASE=141/OFFSET=420: cada metade usa no maximo 411 qwords e as duas
  terminam antes do limite 1024; paleta/luzes terminam em 140.
- Near/CPU clipping expandem apenas o chunk necessario. Skin/morph CPU
  usam o stride compacto e preservam o mapa de reuso, inclusive entre lotes.
  Collision3D resolve os indices ao copiar malhas e subarvores; raycasts
  cobrem transformacao e identidade do triangulo. C consumidores da MeshView
  devem recompilar e usar athena_mesh3d_corner/athena_mesh3d_stream_count.
- Testes host conferem indices locais, alinhamento, ordem, posicoes, UVs,
  targets e stats; leem o payload REF depois de liberar a malha, nos dois
  modos de DMA, com ASan/UBSan. Suite completa C/JS i386 verde. VCL/VSM dos
  tres novos programas conferidos na toolchain; nove VSM passam o checker.
- PCSX2 2.8.2: 11 comparacoes soup/indexed (estatica, diffuse, alpha mask,
  quatro morphs, skin, skin texturizado, skin+morph CPU, near, guard band,
  backface e point lights/fog), capturas inspecionadas. A validacao encontrou
  e corrigiu WAITQ antes do branch de textura na projecao iluminada sem UV.
- A/B native: mesma revisao e configuracao, apenas MODEL3D_INDEXED=0/1;
  tres execucoes por variante, 60 frames de warmup/120 amostras por cena.
  Grade de 289 vertices fonte, 1536 entradas, 512 triangulos, normais
  explicitas. Estatica/skin armazenam 640 vertices com padding e transformam
  576; morph armazena 768 e transforma 640. Compartilhamento entre lotes
  ainda duplica vertices; nao e a compactacao minima global de 289 vertices.
- RAM da malha: 43504 -> 23572 bytes estatica (-45,8%), 83520 -> 40292
  skin (-51,8%), 120376 -> 67676 morph (-43,8%). Payload: 24576 -> 11776
  unlit, 43008 -> 19456 diffuse, 59040 -> 24576 skin, 127376 -> 61440 morph.
  Contagem de triangulos igual; bytes incluem padding e indices locais.
- Draw ate GS ociosa: unlit ~80,8 -> 80,4 us (praticamente empate), diffuse
  ~99,6 -> 94,4 (-5,2%), skin ~399,7 -> 236,6 (-40,8%), morph ~732,5 ->
  541,8 (-26,0%). Submissao estatica isolada sobe ~6%-8%; a reducao de RAM
  e DMA nao implica ganho de CPU em todos os casos. O fence extra pertence
  apenas ao benchmark. Lotes iniciais de 33 entradas pioravam o custo fixo;
  foram substituidos pelos lotes de 48 antes de publicar o resultado.
- Sample reproduzivel: samples/native/3d_indexed_profile/main.c; flag
  MODEL3D_INDEXED padrao 1, 0 usa objetos separados em *-soup. JSON, hashes
  de fontes/ELFs e capturas em docs/benchmarks/3d-indexed-*-p1-2026-10-07.*.
  ELFs native A/B e QuickJS foram copiados para bin, inclusive packed.
- Regressao adicional: as cinco etapas de 3d_geometry concluiram no ELF
  final; skin VU1 passa de 640 para 416 bytes nos casos contidos.
- PS2 real permanece pendente. P10 historico tambem: a baseline registra
  7621811 com alteracoes locais nao commitadas, sem patch exato preservado;
  nao atribuir a regressao antiga a uma mudanca especifica.

### Rodada 11 (07/10/2026): custo por objeto e reuso de deformacao (P10/P1)

- P10: `draw_view` usa a view emprestada e imutavel, eliminando a segunda
  copia completa por objeto. A/B native com o mesmo `3d_regression`: onze
  cenas, 120 amostras apos 60 frames de warmup, todos os contadores iguais.
  Medicao sequencial: inside-batch 626,575 -> 610,558 us (-2,56%);
  inside-individual 829,675 -> 813,908 us (-1,90%). Nao reconstrui nem
  atribui a antiga regressao de ~1% sem o codigo exato daquela baseline.
- P1 (primeira etapa): meshes indexadas com skin/morph preservam um mapa
  uint16 da primeira ocorrencia expandida de cada vertice fonte. CPU
  calcula a deformacao uma vez e replica o resultado. Normais geradas por
  face nao compartilham resultados. Sem indices/duplicatas, ou se faltar
  RAM para o mapa, permanece o caminho anterior. Mapa custa 2 bytes por
  entrada expandida; nao reduz RAM dos streams ou trafego VIF nesta etapa.
- Novo sample native `samples/native/3d_deform_profile/main.c`: grade de
  16x16 cells, 289 vertices fonte, 1536 entradas, 512 triangulos. 25 joints
  e target_normals forcando os caminhos CPU; A/B muda somente a construcao
  do mapa. Skin: 2847,292 -> 885,783 us (-68,89%); morph: 1097,400 ->
  535,392 (-51,21%); ambos: 3775,975 -> 1252,992 (-66,82%). Cada operacao
  calcula 289 vertices; triangles e geometryBytes (24576) permanecem iguais.
- Host compara bit a bit indices vs triangle list: posicoes, normais e
  bounds de skin, morph, ambos e flat normals. Teste ASan/UBSan cobre
  65532 entradas, incluindo a ultima ocorrencia representavel por uint16.
  Suite C/JS completa i386 com ASan/UBSan verde; builds native/QuickJS.
- PCSX2: cinco etapas de `3d_geometry` concluidas (VU1, alpha mask,
  perspectiva e CPU near clipping). JSON e capturas:
  `docs/benchmarks/3d-object-cost-p10-2026-10-07.json`,
  `3d-deform-reuse-p1-2026-10-07.json` e `3d-deform-geometry-p1-2026-10-07.json`.
- Armazenamento compacto e lotes indexados reais no VU1 foram entregues
  na rodada 12, com as mesmas limitacoes de clipping e flat normals.
  PS2 real e atribuicao exata da baseline historica P10 continuam pendentes.

### Rodada 10 (07/10/2026): paletas T4/T8 na VRAM (C3)

- Backend conta cores RGBA ja convertidas para a GS e gera indices T4
  (ate 16 cores, nibble baixo primeiro) ou T8 (ate 256, CLUT CSM1).
  Compara o custo real da alocacao textura+CLUT contra CT32; texturas
  pequenas/mais de 256 cores permanecem CT32, sem quantizacao.
- Falha ao alocar indices ou CLUT tambem preserva o upload CT32. View
  publica e decoder mantem CT32. RAM adicional dos indices/paleta e
  retida para reset de video, e liberada com o backend apos o GS ocioso.
- Host GS ASan/UBSan: limites 1/2/16/17/256/257 cores, mesma cor RGB com
  alpha distinto, todos os indices CSM1, padding, reset, release deferido
  e falha de cada alocacao. Suite completa C/JS com sanitizadores verde.
- `3d_palette.js`: quatro etapas no PCSX2, com deltas exatos de VRAM.
  64x64 T4: 2304 bytes; T8: 5120; CT32: 16384, incluindo a CLUT nos modos
  indexados. Oito texturas 512x512 residentes: 1050624 bytes totais em T4
  contra 8388608 de pixels CT32 (reducao de 87,48%).
- Reset preserva alpha mask; cinco etapas de geometria (skinning VU1,
  perspectiva e clipping CPU) e nove etapas de features completaram.
  JSON/capturas em `docs/benchmarks/3d-palette-*-c3-2026-10-07.*`.
- PS2 real permanece pendente. P1 (indices de geometria) e P10 (custo
  fixo por objeto) sao os proximos pontos abertos.

### Rodada 9 (07/10/2026): ocupacao e tamanho do ring DMA (P9)

- `OWL_DIAGNOSTICS=1` ativa snapshots/reset de contadores OWL: queries,
  flushes por capacidade/canal, esperas por ticket pendente/reuso/fence,
  qwords das chains e pico ocupado por metade. Reset nao altera tickets
  nem descarta dados ja escritos. Builds normais nao possuem os contadores.
- `OWL_PACKET_QWORDS=2048|4096|8192` configura o ring total (16 bytes por
  QW, dividido em duas metades), com diretorios de objetos separados.
  Padrao continua 2048: 32 KiB totais, 16 KiB por metade.
- Host UBSan e suite C/JS i386 ASan/UBSan verdes; testes verificam contagens
  exatas, troca de canal, reset com pacote aberto e flush por capacidade.
  Docker: builds 2048/4096 instrumentados e build padrao sem diagnostico.
- PCSX2: nove cenas por tamanho, 120 amostras apos 60 frames de warmup;
  contadores de submissao identicos. A primeira medicao apenas do draw
  sugeria ~1% de ganho, mas ring maior pode adiar DMA ate o flip.
- Benchmark diagnostico agora mede tambem clear/draw ate FLUSHA + FINISH
  da GS (sem update/HUD/flip). Nessa medida, 4096 piora 1,4%-1,9%, apesar
  de zerar o flush por capacidade (1 -> 0/frame). Scene lit static:
  764,0 -> 777,9 us. Portanto nao aumentar o padrao; 4096 custa +32 KiB.
- Esse fence extra pertence apenas ao benchmark com diagnostico ligado.
  Contadores de transporte incluem HUD/flip; esperas pendentes nao sao
  prova de stall real. Qwords submetidos nao incluem payload externo REF.
- [Resultado completo](../docs/benchmarks/3d-ring-p9-2026-10-07.json),
  [2048](../docs/benchmarks/3d-ring-2048-p9-2026-10-07.png),
  [4096](../docs/benchmarks/3d-ring-4096-p9-2026-10-07.png).
  Uma execucao por tamanho/modo no emulador. PS2 real ainda pendente;
  P1 (indices), C3 (paletas na VRAM) e P10 continuam abertos.

### Rodada 8 (07/10/2026): DMA_REF e A/B (P2)

- O mesmo escritor de streams REF agora e testado no host: mapper de
  enderecos preserva ponteiros de 32/64 bits, sem assumir enderecos fisicos.
  O simulador percorre a chain no wait, apos o descarte da instancia, e
  le o payload externo inteiro (inclusive padding) com sanitizadores.
- `tests/host/run_3d.sh` executa o teste de pacotes em inline e REF; 17
  execucoes C com ASan/UBSan, mais suite completa JS i386, verdes.
  A fila retida cresce alem de 64 entradas e atravessa varias geracoes
  com 512 malhas descartadas imediatamente apos draw.
- Build `RENDER3D_DMA_REF=0` permite A/B sem editar fontes. Objetos ficam
  em diretorio separado `*-inline` para evitar mistura de flags. O modo
  inline nao retem malhas cuja geometria ja pertence ao pacote.
- Docker: dois ELFs native do mesmo `samples/native/3d_profile/main.c`;
  PCSX2 Flatpak: nove etapas por variante, 120 amostras apos 60 frames de
  warmup. Draw nao inclui update, HUD nem flip.
- Draw: unlit static 799,1 -> 639,6 us (-20%); batch lit static
  1278,6 -> 991,5 us (-22,5%); scene lit static 1021,2 -> 733,7 us
  (-28,2%). Demais cenas entre -20,2% e -28,2%; todos os contadores de
  submissao e geometryBytes identicos. geometryBytes e payload transmitido,
  nao ocupacao do ring.
- [A/B completo](../docs/benchmarks/3d-dma-ref-p2-2026-10-07.json),
  [inline](../docs/benchmarks/3d-dma-inline-p2-2026-10-07.png),
  [REF](../docs/benchmarks/3d-dma-ref-p2-2026-10-07.png).
  Uma execucao por variante; tempos do emulador. PS2 real ainda pendente.
  P1 (vertices indexados), C3 (T4/T8 em VRAM), P9 e P10 continuam abertos.

### Rodada 7 (07/10/2026): skinning texturizado no VU1 (P6)

- UVs em V2_32 no qword 162; output passa para 195, sem sobrepor a
  paleta de 24 joints, as luzes ou a segunda metade do double buffer VU1.
- Microprograma calcula ST/Q, escala RGB 128 para MODULATE / 255 sem
  textura, preserva alpha GS e usa a mesma iluminacao, culling e fog.
- Passes skinned usam identidade da textura, bind/restauracao de GS e alpha
  mask; UVs ausentes sao recusados. CPU continua para bounds que cruzam
  planos e skins acima do limite VU1.
- Testes host: packets com 33 vertices (dois chunks), padding, UVs, pesos,
  contadores, alpha mask, reuso do pass e troca para objeto sem textura;
  Scene3D/glTF texturizado em VU1 e CPU com a mesma deformacao/paleta.
- Suite completa C/JS i386 com ASan/UBSan verde; build EE QuickJS no Docker;
  `.vsm` regenerado e checker de hazards OpenVCL verde.
- PCSX2: cinco etapas (morph, skin, unlit-mask, perspective, near) concluidas.
  [Resultados](../docs/benchmarks/3d-skin-texture-p6-2026-10-07.json),
  [skin](../docs/benchmarks/3d-skin-texture-p6-2026-10-07.png),
  [alpha mask](../docs/benchmarks/3d-skin-texture-mask-p6-2026-10-07.png).
- A/B com mesmo codigo e asset, mudando apenas a elegibilidade de textura
  em Scene3D: 5190,2 -> 700,2 us (1 coluna), 20685,4 -> 2737,5 us (4).
  Triangulos preservados: 778 / 3112. VU1 envia mais bytes (joints/pesos)
  e usa chunks menores, mas remove a deformacao e iluminacao por vertice no EE.
  [Medicao](../docs/benchmarks/3d-skin-texture-profile-p6-2026-10-07.json).
  Uma execucao por variante, 120 amostras apos 60 frames de warmup; tempos
  de draw no emulador, sem update/flip. PS2 real permanece pendente.

### Rodada 6 (07/10/2026): lifetime DMA (P2)

- OWL nao reutiliza numeros de tickets apos `owl_init`: malhas que sobrevivem
  ao reset nao confundem a geracao nova com a antiga na deduplicacao da retencao.
- Renderer drena referencias ja consumidas antes do culling e ao abrir grupo,
  mesmo sem geometria visivel. Nao adiciona espera DMA nesses caminhos.
- Testes de regressao cobrem tickets crescentes, claim da mesma malha apos
  reset, deduplicacao por geracao e liberacao por grupo vazio ou objeto culled.
- Host: 16 suites UBSan e ASan/UBSan. Build EE native no Docker.
- PCSX2 Flatpak: nove etapas de `3d_textures` concluidas; imagem apos reset
  preserva as tres geometrias texturizadas. [Resultado](../docs/benchmarks/3d-dma-reset-2026-10-07.json)
  e [captura](../docs/benchmarks/3d-dma-video-reset-2026-10-07.png).
- O teste host ainda usa copia inline: nao valida a leitura de enderecos REF.
  A/B de performance e validacao em PS2 real permanecem pendentes em P2.

### Rodada 5 (07/10/2026): D5

- Cache fraco de wrappers de Node, em tabela por runtime/no. Nao guarda
  referencias JS nem referencias nativas adicionais. O wrapper retido ainda
  possui exatamente uma referencia nativa; acessos repetidos so duplicam
  o mesmo JSValue.
- root/getParent/getChild retornam o objeto do construtor, inclusive subclasses;
  o loader glTF usa a mesma tabela. Dispose invalida todos os aliases desse
  objeto. Se o grafo retiver o no, o proximo acesso cria outro wrapper.
- Finalizers removem a entrada antes do release nativo; um wrapper antigo
  descartado/finalizado nao apaga a entrada de seu substituto. O no emprestado
  e retido antes da alocacao do wrapper, que pode disparar GC.
- Correcao adicional de lifetime: Node ctor le newTarget.prototype antes
  de obter o mesh nativo; um getter/proxy que descarta o mesh provoca TypeError
  em vez de deixar um ponteiro pendente.
- Docker i386: suite JS completa verde com ASan/UBSan, incluindo 16 suites
  host C. Scene3D passou 595 checks por runtime, em duas execucoes, incluindo
  256 wrappers com ciclos coletados enquanto seus nos sobrevivem no grafo.
- Build EE QuickJS verde, com imagem `athenaenv-build`, em copia isolada
  `/tmp/athena-d5-build`. ELF em `bin/athena_3d_node_identity_js.elf` e `_pkd.elf`.
- PCSX2 Flatpak v2.8.2 (Vulkan/RADV): `bin/3d_node_identity.js` concluiu
  constructor, dispose, cyclic-gc e gltf, com asserts de identidade por frame.
  As capturas confirmam que os nos continuam desenhados apos GC/descarte.
- Evidencias: [resultados](../docs/benchmarks/3d-node-identity-d5-2026-10-07.json),
  [GC](../docs/benchmarks/3d-node-identity-cyclic-gc-2026-10-07.png),
  [glTF](../docs/benchmarks/3d-node-identity-gltf-2026-10-07.png).
- Tipos/catalogos publicos atualizados. PS2 real permanece pendente;
  nao foi medido ganho de performance nesta rodada.

### Rodada 4 (07/10/2026): D6 e D7

- `Mesh.fromGeometry` aceita joints/weights e targetPositions/targetNormals,
  todos copiados. Numero de targets inferido pelo tamanho do stream de deltas;
  indices expandem todos os streams na mesma ordem. Getters terminam antes
  de obter ponteiros dos buffers, e erros preservam as excecoes do JS.
- Normalizacao dos pesos de skin pela maior componente: entradas finitas
  cuja soma transborda (ex.: 3e38 + 3e38) continuam somando 1, e weights8 255.
- Host: 16/16 suites com UBSan e ASan/LeakSanitizer. Teste nativo verifica
  pesos extremos, indices e a soma quantizada.
- JS: suite completa no Docker i386 com ASan/UBSan verde; nova suite
  `geometry3d_test.js`, 70 checks por runtime, duas execucoes. Cobre tipos,
  limites, subarray, getters, ownership/GC, morph bounds e geometria
  procedural substituindo a malha de um skin glTF existente.
- Builds EE QuickJS e native verdes em copia isolada `/tmp/athena-d6-build`;
  ELF de visual em `bin/athena_3d_geometry_js.elf` (e `_pkd.elf`).
- PCSX2 Flatpak v2.8.2, Vulkan/RADV: `bin/3d_geometry.js` concluiu morph
  (VU1 + CPU, um vuMorphObjects) e skin (VU1 + CPU texturizado), quatro
  triangulos e dois objetos em cada etapa. Streams alterados apos a copia
  nao modificam o resultado. Capturas conferidas visualmente.
- Evidencias: [contadores](../docs/benchmarks/3d-geometry-d6-2026-10-07.json),
  [morph](../docs/benchmarks/3d-geometry-morph-2026-10-07.png),
  [skin](../docs/benchmarks/3d-geometry-skin-2026-10-07.png).
- Tipos e catalogos publicos regenerados; PS2 real continua pendente.

### Rodada 3 (07/10/2026): M4

- Camera alterada ou reinicializada no mesmo endereco abre outro pass; uma
  copia sem alteracoes pode reutilizar o estado. Setters invalidos preservam
  a revisao. Texturas novas recebem identidades diferentes mesmo apos release.
- Testes host: 16/16 suites com UBSan e ASan/LeakSanitizer; teste de pacotes
  cobre mutacao/reinit/copia da camera dentro do grupo. O teste de retencao
  DMA agora drena malhas de etapas anteriores antes de contar releases.
- Suite JS completa no Docker i386 com ASan/UBSan: verde.
- Builds EE QuickJS e native no Docker `athenaenv-build`, em copia isolada
  em `/tmp/athena-m4-build`.
- PCSX2 Flatpak v2.8.2, Vulkan/RADV: nove etapas de `3d_features` concluidas;
  `camera-group` mostra dois quads separados e registra dois passes. O grupo
  de 64 objetos continua em um pass; churn estabiliza em 4.121.392 bytes.
- Regressao native: 11 etapas registradas. Esta execucao inclui o trabalho
  DMA_REF ja presente no checkout; os tempos nao isolam o custo de M4.
- Resultados: [features](../docs/benchmarks/3d-features-m4-2026-10-07.json),
  [native](../docs/benchmarks/3d-native-m4-2026-10-07.json),
  [captura](../docs/benchmarks/3d-camera-group-2026-10-07.png).
- PS2 real e medicao A/B de performance continuam pendentes.

### Rodada 2 (07/10/2026)

- Host: `tests/host/run_3d.sh` 16/16, com UBSan e com ASan/LSan.
- JS: `sg docker -c "docker compose run --rm js-tests"` (i386 + ASan/UBSan) verde.
- EE: build via imagem `athenaenv-build` (Docker), modulos 3D configurados com
  `node tools/modules.js configure` (arquivos gerados restaurados depois).
- PCSX2 (flatpak, `-batch -fastboot`, capturas com `gnome-screenshot`):
  - `bin/3d_features.js` (nova, ver `docs/3D_REGRESSION.md`): wrap, paleta com
    indices nas faixas trocadas do CSM1, alpha mask, GLB com imagem embutida e
    MASK, cubico (x medido -0,75 linear / -1,03 cubico em t=0,375 s, valores
    esperados), `Render3D.group` (1 pass para 64 objetos), churn de texturas
    (memoria estavel em ~4,114 MB ao longo de 33 texturas).
  - `athena_3d_regression_native`: contadores identicos a baseline nas 11
    etapas; tempo ver P10 (ruido entre execucoes do mesmo ELF: ~0,1%).
  - `athena_3d_textures_native` e `athena_3d_lighting_native`: contadores
    iguais aos documentados; imagens corretas, inclusive `recreate` e
    `video-reset` com a liberacao adiada.
  - `athena_3d_profile_native`: A/B do P4 (acima).
- Bug achado pela validacao visual e corrigido: o `Model3D.load()` do JS passa
  sempre um material padrao, que anulava o `alphaMode: MASK` do arquivo.

### Rodada 1

Executado nesta rodada:

- `tests/host/run_3d.sh`: 16/16 suites, com UBSan (padrao) e com
  `ATHENA_3D_SANITIZERS=address,undefined` (ASan + LeakSanitizer, x86_64).
- Testes novos ou ampliados:
  - `render3d_packet_test.c`: luzes enviadas uma vez por stamp dentro do pass,
    reenvio apos mudanca e para outro objeto `Lights` com o mesmo conteudo;
    escala 0 conta como culled; matriz singular falha com detalhe.
  - `texture3d_test.c`: UVs tileadas validas, limite `UV_LIMIT`, clipping com
    UV fora de [0,1], OBJ com `map_Kd` (repeat) e recusa de dois `map_Kd`.
  - `texture3d_gs_test.c`: expansao T8 (CLUT CSM1), T4 (nibble baixo primeiro)
    e CT16; registrador CLAMP para os quatro modos de wrap.
  - `gltf3d_test.c` + `tests/host/3d/shared_texture.gltf`: sampler ausente =
    REPEAT/LINEAR, textura compartilhada entre primitivas, `normalTexture`
    ignorado.
  - `scene3d_test.c`: joint ancestral do no skinned nao vaza a subarvore; skin
    que sobrevive ao joint; agrupamento por textura (2 passes em vez de 3);
    seguidores de morph.
  - `animation3d_test.c`: player com overflow e pausado sem impedir os demais.
  - `three_d_test.c`: quad OBJ triangulado; detalhe de erro dos loaders.
  - `bin/tests/textures3d_test.js`: UV_LIMIT, wrap, mensagem com caminho,
    `Render3D.group`.

Nao executado (fazer antes do merge):

- Suite JS (`docker compose run --rm js-tests`, i386 + ASan): o usuario desta
  maquina nao tem permissao no socket do Docker. Os bindings alterados passaram
  apenas por checagem de sintaxe.
- Build EE (`PS2DEV` nao configurado nesta maquina).
- PCSX2/PS2 real: repeat na GS (C1), expansao de paletas reais (C3) e o ganho
  de pacotes do P4 precisam de confirmacao visual/medicao.
