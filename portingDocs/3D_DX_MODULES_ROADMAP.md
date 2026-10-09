# Roadmap de novos modulos para jogos 3D (developer experience e voxel)

Proposta de modulos novos que aumentam a produtividade de quem faz jogos 3D
com o AthenaEnv, incluindo um modulo nativo de voxel. As Fases 1, 2 e 3
foram implementadas em 2026-10-09 (registros nas secoes 3, 4 e 5); os
demais itens seguem propostos. Todos seguem `NEW_MODULE_DIRECTIVES.md` e sao
**aditivos** (diretorio novo em `src/modules/<id>/` com `module.json`), salvo
quando marcado como "extensao de modulo existente".

Base: branch `modular-3D` @ `86d533e` e o estudo
`docs/minecraft-feasibility/` (benchmarks no PCSX2 2.8.2 em 2026-10-09).

Legenda dos numeros:

- **MEDIDO**: benchmark no PCSX2 (`docs/minecraft-feasibility/performance.md`).
  PCSX2 nao reproduz o tempo de GS/VU1/cache do console; os valores sao
  indicativos.
- **ESTIMADO**: derivado de medidas ou do codigo; precisa ser confirmado.
- **VERIFICADO**: lido no codigo-fonte.

Legenda de status: **PROPOSTO** (nenhum codigo), **EM ANDAMENTO**,
**VALIDAR NO HW** (implementado e testado no host e no PCSX2), **FEITO**.

## 1. Lacunas atuais (motivacao)

| Lacuna | Evidencia |
|---|---|
| `Debug` desenha em "world space" so em 2D (via `Camera2D`); nao ha desenho de depuracao 3D | `src/modules/debug/debug.d.ts:38-39,161-165` |
| `Render3D` desenha so listas de triangulos (sem linhas) | `src/modules/render3d/native/render3d_gs.c:144` |
| `Camera3D` expoe so matrizes; sem `worldToScreen`/`screenToRay` | `src/modules/camera3d/camera3d.d.ts` |
| `Scene3D` sem raycast/picking e sem LOD | `src/modules/scene3d/scene3d.d.ts` |
| `Sound` tem so `pan`/volume; sem audio posicional | `src/modules/sound/sound.d.ts:49,265` |
| `Gamepad` e entrada crua; cada jogo refaz dead zone, curvas e rebinding | `src/modules/gamepad/gamepad.d.ts` |
| `Scene.Assets` so tem loaders 2D (aceita `define()` de tipos novos) | `src/modules/scene/scene.d.ts:349-374` |
| Medir tempo no JS e caro: `System.getMilliseconds()` ~0,1 ms por chamada | MEDIDO (`performance.md` §1) |
| `Render3D.draw` aloca um objeto de stats por chamada se `out` nao for passado: 144 us vs 22-30 us | MEDIDO (`performance.md` §6) |
| Iterar blocos em JS e lento (15-25 us/bloco; ruido 3D 237 us/amostra) | MEDIDO (`performance.md` §1-2) |
| Chunk que contem a camera vai para o clipper em C no EE (~5 ms por 16^3) | MEDIDO + VERIFICADO (`render3d_gs.c:531-537`) |
| Nao existe funcao publica que embrulhe `AthenaMesh3D*` nativo num `Model3D.Mesh` JS | VERIFICADO (`src/modules/model3d/include/athena/js/model3d.h`) |

## 2. Visao geral do roadmap

| Fase | Modulos | Foco | Esforco total (ESTIMADO) |
|---|---|---|---|
| 1 | `debug3d`, `profiler`, picking/projecao, ajustes de ergonomia | Ver e medir o que acontece | 3-4 semanas |
| 2 | `input`, `assets3d`, `savegame`, `replay` | Fluxo de jogo e iteracao | 3-4 semanas |
| 3 | `voxel`, `meshbuilder` | Desempenho onde o JS e o gargalo | 4-6 semanas |
| 4 | `lod`, `audio3d`, `triggers3d`, `nav`, `sky` | Sistemas de jogo 3D | 5-7 semanas |
| 5 | `dev` (live reload), `bench`, `ui`, CLI de assets | Ferramentas e conteudo | 3-4 semanas |

Ordem pensada para que cada fase aproveite as anteriores: `debug3d` e
`profiler` sao usados para validar todos os modulos seguintes; `replay` e
`bench` tornam as medicoes reprodutiveis; `meshbuilder` e `voxel`
compartilham o gerador de geometria.

Dependencias principais:

```
debug3d ──┐
profiler ─┼──> (validacao de tudo que vem depois)
picking ──┴──> triggers3d, nav (consultas espaciais)
input ───────> replay (grava acoes)
meshbuilder ─> voxel (geracao de malha), lod (malhas simplificadas)
assets3d ────> CLI de assets (formato pre-processado)
savegame ────> voxel (salvar regioes alteradas)
```

## 3. Fase 1: visibilidade e medicao

### Registro da Fase 1 (2026-10-09)

Implementada sem alterar a selecao de modulos do repositorio (o build de
validacao foi feito numa copia com `configure` incluindo os modulos 3D,
`debug`, `profiler` e `debug3d`). Validacao:

- testes host (Docker `js-tests`, i386 + ASan/UBSan): toda a suite passa,
  incluindo `bin/tests/profiler_test.js` (45), `bin/tests/debug3d_test.js`
  (35, dois runtimes), `scene3d_test.js` (626) e `three_d_test.js` (92);
- PCSX2 2.8.2, ELF de validacao: `profiler_test.js` e `debug3d_test.js`
  passam; um script de console (picking + `Render3D` com draws reais) passou
  32/32; `bin/debug3d_example.js` roda a 60 FPS com overlay e picking;
- build EE sem warnings nos arquivos novos/alterados; `RUNTIME=native`
  compila todos os fontes (o link so falta `athena_main`, esperado);
- PS2 real: **pendente** (nenhum item abaixo esta FEITO ate la).

Numeros MEDIDOS no PCSX2 (`Date.now()` sobre 300 a 2000 repeticoes;
resolucao de ~3 us; PCSX2 nao reproduz o tempo do console):

| Medida | Valor |
|---|---|
| `Render3D.draw` (20 triangulos): stats novo / `out` reusado / `null` | 150 / 33 / 23 us |
| `Profiler.begin/end` por id / por nome | 5,5 / 10,5 us |
| `camera.worldToScreen` / `screenToRay` (`out` reusado) | 10 / 13 us |
| `scene.raycast`, 200 nos: preciso / so AABB | 113 / 137 us |
| `scene.queryBox` com 10 resultados (refill do array) | 347 us |
| `Debug3D`: 2.000 segmentos (fila via `lines()` + projecao + pacotes GS) | 7,3 ms/frame |

### 3.1 `debug3d`: desenho de depuracao em 3D (VALIDAR NO HW)

> **Implementado** em `src/modules/debug3d/`. API final: `line`, `lines`
> (Float32Array), `box` (com `Matrix4` opcional), `sphere`, `axes`, `grid`,
> `frustum`, `normals` (Instance ou Mesh), `setCamera` (sistema nativo do
> Loop, antes do overlay do `debug`), `draw(camera, dt)` para loops proprios,
> `age`, `clear`, `count`, `dropped`, `show`. Diferencas do esboco: duracao
> e um argumento `seconds` (sem objeto de opcoes); **sem depth test** (as
> linhas ficam sempre por cima); `nodeBounds` fica no JS
> (`node.getWorldBounds()` + `box`). Projecao/clipping em C no EE
> (`debug3d.c`, testavel no host); envio por `draw_line_list` em lotes de 256.
> **Criterio de saida nao atingido:** 2.000 linhas custam 7,3 ms no PCSX2
> (alvo < 2 ms). Proximo passo: transformar os vertices com VU0 (como
> `camera3d.c` ja faz para caixas) e medir no PS2 real.

- **Objetivo:** desenhar linhas, caixas, esferas, eixos, grade, frustum e
  normais em espaco de mundo, com a camera 3D, por um frame ou por N segundos.
- **Usos:** ver AABBs de culling e de colisao, raios de `Collision3D.raycast`,
  hitboxes, caminhos de IA, frustum de uma segunda camera, normais de malhas
  procedurais (`Mesh.fromGeometry`), limites de chunks voxel.
- **Ganhos:** elimina o "debug por console.log" de geometria. Hoje nao ha
  como ver um AABB 3D sem criar uma malha so para isso. Erros de winding,
  bounds e transformacao passam a ser visiveis na hora.
- **API esboco:**
  ```ts
  Debug3D.setCamera(camera);
  Debug3D.line(a, b, color?, { seconds?, depthTest? });
  Debug3D.box(min, max, color?, opts?);   Debug3D.sphere(center, r, color?, opts?);
  Debug3D.axes(matrix, size?);  Debug3D.grid(size, step, color?);
  Debug3D.frustum(otherCamera, color?);  Debug3D.normals(instance, length?, color?);
  Debug3D.nodeBounds(node, color?);      // AABB da subarvore (Scene3D ja calcula)
  ```
- **Implementacao:** nativo. Primitivas de linha na GS (`GS_PRIM_LINE`) num
  pass proprio apos o 3D, transformadas no EE ou num microprograma VU1
  simples; buffer de comandos em C com expiracao. Integra-se ao toggle do
  `debug` (`Debug.toggleWith`). Desligado nao custa nada.
- **Esforco:** 1-1,5 semana.
- **Criterio de saida:** 2.000 linhas por frame em < 2 ms de EE no PCSX2;
  teste host do pacote; exemplo `bin/debug3d_example.js`.

### 3.2 `profiler`: marcadores de CPU e estatisticas de render (VALIDAR NO HW)

> **Implementado** em `src/modules/profiler/` (nativo + camada JS
> `Profiler`, depende de `loop` e `debug`). Relogio: registrador COP0 Count
> do EE (uma instrucao). API final: `scope`/`counter` (ids), `begin`/`end`,
> `measure(name, fn)` (o esboco chamava de `scope(fn)`), `count`, `frame`,
> `auto` (sistema do Loop), `stats` (last/average/p95/peak sobre 120
> frames), `attachRender3D(Render3D)` (contadores `3d.*` via
> `Render3D.frameStats()` e aviso de clipper em C no log), `overlay`
> (uma linha por escopo via `Debug.watch`), `report`, `log`, `reset`.
> Criterio de saida: par `begin/end` 5,5 us por id no PCSX2 (alvo < 15 us,
> atingido no emulador).

- **Objetivo:** medir onde vai o tempo do frame com custo baixo e mostrar no
  overlay do `debug`, junto com as estatisticas do `Render3D`.
- **Usos:** descobrir se o frame e limitado por JS, por submissao de draw ou
  pelo clipper em C; comparar antes/depois de otimizacoes; detectar picos
  (rebuild de chunk, carga de asset, GC).
- **Ganhos:** hoje medir custa ~0,1 ms por leitura de relogio (MEDIDO), o
  que distorce medicoes finas. Marcadores nativos leem o contador do EE
  diretamente. Mostrar `clippedTriangles`/`rejectedTriangles` e alertar quando
  o clipper em C domina teria revelado, sem benchmark dedicado, o maior custo
  em primeira pessoa encontrado no estudo.
- **API esboco:**
  ```ts
  Profiler.begin("meshing"); ... Profiler.end("meshing");
  Profiler.scope("ai", () => updateAI());
  Profiler.attachRender3D(statsObject);   // acumula Stats por frame
  Profiler.overlay(true);                 // barras por escopo + contadores 3D
  Profiler.capture(frames): ProfileReport; // media, p95, max por escopo
  ```
- **Implementacao:** nativo (ring buffer de marcadores com `GetTimerSystemTime`),
  desenho reaproveitando o grafico de frame do `debug`.
- **Esforco:** 1 semana.
- **Criterio de saida:** custo de um par `begin/end` < 15 us no PCSX2;
  relatorio identico entre duas execucoes deterministicas.

### 3.3 Picking e projecao (extensao de `camera3d` e `scene3d`) (VALIDAR NO HW)

> **Implementado.** `Camera3D`: `setViewport(w, h)` (padrao 640x448),
> `worldToScreen(x, y, z, out?)` -> `{x, y, depth}` ou `null` atras da
> camera, `screenToRay(sx, sy, out?)` -> `{x, y, z, dx, dy, dz}` (sem
> inverter matriz). `Scene3D`: `raycast(ray, maxDistance?, {precise?},
> out?)` -> `{node, distance, x, y, z, nx, ny, nz, triangle}` (triangulos
> nas duas faces, ou so AABB; poda por bounds de subarvore) e
> `queryBox(min..., max..., out?)`. Malhas skinned sao ignoradas e malhas
> com morph usam a pose base (documentado). Sem `mask` (os nos nao tem
> camadas).

- **Objetivo:** converter entre mundo e tela e fazer raycast contra a cena
  sem montar um `Collision3D.World`.
- **Usos:** selecionar objetos com cursor ou mira, colocar UI 2D sobre
  personagens (nomes, barras de vida), mira de tiro, editor em jogo,
  "olhar para" interativo.
- **Ganhos:** hoje cada jogo precisa inverter matrizes em JS (aritmetica em
  `double` emulada, lenta no EE) ou duplicar a cena num `Collision3D.World`
  (~172 bytes por triangulo e rebuild completo do BVH a cada mudanca,
  VERIFICADO em `collision3d.c:8,12,250-296`).
- **API esboco:**
  ```ts
  camera.worldToScreen(x, y, z, out?): [sx, sy, depth] | null;
  camera.screenToRay(sx, sy, out?): { origin, direction };
  scene.raycast(origin, dir, maxDist, { mask?, precise? }): { node, distance, point, normal } | null;
  scene.queryBox(min, max, out?): Node[];
  ```
- **Implementacao:** nativo. AABBs de subarvore ja existem no `scene3d`;
  teste fino opcional por triangulo nas streams imutaveis da malha.
- **Esforco:** 1 semana.

### 3.4 Ajustes de ergonomia nos modulos existentes (VALIDAR NO HW)

> **Implementado, compativel com o codigo existente.** (1) `stats = null`
> em `Render3D.draw`, `Batch.draw`, `scene.draw` e `scene.update` devolve
> `undefined` sem montar objeto (antes `null` lancava TypeError); o retorno
> padrao nao mudou, porque exemplos guardam e comparam os objetos
> devolvidos. Novo `Render3D.frameStats(out?, reset = true)` com os totais
> do frame. MEDIDO: 150 -> 23 us por draw. (2) Novo campo
> `cpuClipObjects` em `Render3D.Stats` (objetos recortados em C no EE) e
> aviso no `Profiler`. (3) `athena_mesh3d_js_wrap(ctx, mesh)` exportado em
> `include/athena/js/model3d.h`.

| Ajuste | Ganho |
|---|---|
| `Render3D.draw` sem `out` nao aloca objeto de stats (retornar `undefined` ou um objeto interno reutilizado) | MEDIDO: 144 us -> 22-30 us por chamada |
| Aviso opcional (via `debug`/`profiler`) quando um objeto cai no clipper em C | Expoe o maior custo de primeira pessoa (`performance.md` §6) |
| Exportar `athena_model3d_js_wrap_mesh(ctx, AthenaMesh3D*)` em `include/athena/js/model3d.h` | Permite que `voxel`/`meshbuilder` devolvam `Model3D.Mesh` sem copia extra (~11 us/quad economizados, MEDIDO) |

Estes tres sao mudancas pequenas em modulos existentes, nao modulos novos.

## 4. Fase 2: fluxo de jogo

### Registro da Fase 2 (2026-10-09)

Quatro modulos novos, opt-in, sem mudar a selecao de modulos do repositorio
(build de validacao numa copia com `configure` incluindo os novos, `gltf3d`,
`animation3d` e `archive`). Validacao:

- testes host (Docker `js-tests`): `input_test.js` (33), `replay_test.js`
  (15), `savegame_test.js` (23, cartao falso do runner) e
  `assets3d_test.js` (11, dois runtimes) passam, com o resto da suite;
- PCSX2 2.8.2: os quatro testes passam; o SaveGame gravou, listou, leu e
  removeu no Memory Card emulado com o gzip real do `Archive`; o Replay
  gravou e leu o arquivo no host fs;
- PS2 real: **pendente**.

Numeros MEDIDOS no PCSX2 (`Date.now()` sobre 2.000 repeticoes):

| Medida | Primeira versao | Otimizada |
|---|---|---|
| `Input.Map.update`, preset `shooter` (9 acoes, 2 sticks) | 648 us | 217 us |
| `Replay.Recorder.capture`, 1 jogador | 221 us | 75 us |
| `SaveGame.encode`, 7 KB de JSON (-> 1,5 KB gzip) | 120 ms | 40 ms |
| `SaveGame.load` do mesmo save | 315 ms | 28 ms |
| `SaveGame.save` (escrita atomica no cartao, em worker) | - | 488 ms |

As otimizacoes: laços indexados sem iteradores, nada alocado por frame,
tabela para os sticks do Replay, e UTF-8/CRC-32 em C no SaveGame
(`SaveGameNative`). O `encode` ainda roda na thread principal
(`JSON.stringify` + gzip nivel 9); o `save` em si nao trava os frames.
Proximo passo do `input`, se 217 us por frame pesar: o nucleo do
`update()` em C (o roadmap ja previa "nativo leve").

### 4.1 `input`: mapa de acoes (VALIDAR NO HW)

> **Implementado** em `src/modules/input/` (JS, depende de `gamepad`).
> API final: `Input.button(...masks)` (cada mascara e uma combinacao; varias
> sao alternativas), `Input.axis(neg, pos)`, `Input.stick(side, {deadZone,
> curve, sensitivity, invertX, invertY, dpad})`, `Input.preset("platformer"
> | "shooter" | "menu")`, `new Input.Map(bindings, {player | source})` com
> `update`, `pressed`, `justPressed`, `justReleased`, `heldFrames`, `value`,
> `axis` (objeto reusado), `x`, `y`, `rebind`, `bindings()`/`load()`
> (dados simples, para salvar com o SaveGame) e `setSource()` (Replay ou
> dublê de teste). As bordas sao do proprio Map, entao funcionam com
> qualquer fonte. O Map zera o `deadzone` do jogador do Gamepad e aplica o
> de cada stick. Sem triggers analogicos por pressao nesta versao.

- **Objetivo:** acoes e eixos nomeados sobre o `gamepad`, com presets.
- **Usos:** controles de FPS e terceira pessoa, menus, rebinding nas
  opcoes, suporte a varios jogadores e multitap sem `if` por botao.
- **Ganhos:** remove codigo repetido de dead zone, curva de sensibilidade,
  inversao de eixo e "justPressed" por acao; rebinding persistente com
  `savegame`; base para `replay` (grava acoes, nao botoes).
- **API esboco:**
  ```ts
  const map = new Input.Map({
    move: Input.stick("left", { deadZone: .15 }),
    look: Input.stick("right", { curve: "quadratic", invertY: false, sensitivity: 2.5 }),
    jump: Input.button(Gamepad.CROSS),  attack: Input.button(Gamepad.R2),
  });
  map.update(); map.axis("look").x; map.justPressed("jump"); map.rebind("jump", Gamepad.CIRCLE);
  ```
- **Implementacao:** JS ou nativo leve (eixos calculados em C para evitar
  `double` por frame).
- **Esforco:** 1 semana.

### 4.2 `assets3d`: carga 3D gerenciada (VALIDAR NO HW)

> **Implementado** em `src/modules/assets3d/` (JS, depende de `scene` e
> `model3d`; `gltf` usa o GLTF3D quando presente). Tipos `meshes`,
> `textures3d` (com `upload: true` para residir na VRAM durante o loading)
> e `gltf` no `Scene.Assets`, com contagem de referencias e `dispose` no
> ultimo holder (gltf: clips, nos e raiz destacada). **Diferenca do
> esboco:** o parse continua sincrono; as cargas entram numa fila executada
> entre frames dentro de `budgetMs` (padrao 8 ms, ao menos uma por frame),
> por um sistema do Loop ou `Assets3D.update()`. `Model3D.loadMemory` +
> leitura em worker (`Thread.readFileAsync`) ficam **PENDENTE**: exigem
> mudar os loaders nativos (OBJ/glTF/PNG a partir de memoria).

- **Objetivo:** tipos `mesh`, `gltf` e `texture3d` no `Scene.Assets`, com
  contagem de referencias, carga em segundo plano e tela de loading.
- **Usos:** trocar de fase sem vazar malhas e texturas; pre-carregar a
  proxima area; manifestos por cena.
- **Ganhos:** hoje `Model3D.load`/`GLTF3D.load` sao sincronos e o
  `dispose()` e manual (memoria nativa nao pressiona o GC, VERIFICADO em
  `ath_env.c:401-406`). Ref-count centralizado evita vazamentos e
  liberacoes prematuras; a leitura de arquivo pode ir para worker
  (`Thread.readFileAsync` ja existe) deixando so o parse no thread principal.
- **Implementacao:** JS sobre `Scene.Assets.define`; parse em memoria exige
  uma variante `Model3D.loadMemory` (extensao pequena).
- **Esforco:** 1 semana.

### 4.3 `savegame` (VALIDAR NO HW)

> **Implementado** em `src/modules/savegame/` (JS + codec C pequeno;
> depende de `memcard`, usa `Archive` se estiver no build). API final:
> `define({directory, title, icon, version, migrate, compress, port})`,
> `await save(slot, data, {title})`, `await load(slot)` (null se vazio),
> `await remove(slot)`, `exists`, `list`, `status`, `encode`/`decode`,
> `crc32`. Formato: cabecalho "ASAV" + versao dos dados + tamanho + CRC-32,
> payload JSON (gzip opcional); escrita `atomic` em worker. Erros
> `SaveGame.Error` com `code` (os do MemoryCard + CORRUPT, NEWER_VERSION,
> OLD_VERSION, NOT_DEFINED, NOT_AVAILABLE). Sem icone, nao grava
> `icon.sys` (o save nao aparece no navegador do PS2).

- **Objetivo:** slots de save com versao, migracao, compressao e icone.
- **Usos:** progresso, configuracoes, rebinding, regioes alteradas de um
  mundo voxel.
- **Ganhos:** junta `MemoryCard` (`*Async`, `atomic`, `createIconSys`) e
  `Archive.gzip` num fluxo unico e seguro; trata cartao ausente, cheio ou
  sem formatacao com erros padronizados; migracoes evitam saves
  incompativeis entre versoes do jogo.
- **API esboco:** `SaveGame.define({ title, icon, version, migrate })`,
  `await SaveGame.save(slot, data)`, `await SaveGame.load(slot)`, `SaveGame.list()`.
- **Implementacao:** JS sobre `memcard` + `archive`.
- **Esforco:** 3-5 dias.

### 4.4 `replay`: gravacao e reproducao de entrada (VALIDAR NO HW)

> **Implementado** em `src/modules/replay/` (JS, sem dependencias).
> `new Replay.Recorder(sources, {seed, maxFrames})` com `capture()`,
> `source(i)` (valores quantizados como gravados, para o jogo ler os mesmos
> valores ao gravar e ao reproduzir), `toArrayBuffer()`, `save(path)`;
> `Replay.load(path)` / `new Replay.Playback(buffer)` com `advance()`,
> `source(i)`, `seed`, `restart()`. Formato "ARPL": 16 bytes de cabecalho +
> 6 bytes por jogador por frame (1 h a 60 Hz com 1 jogador = 1,3 MB). O
> teste confirma que a reproducao repete a execucao gravada quadro a
> quadro. O `seed` e so armazenado: o jogo semeia seus geradores
> (`Random.seed(play.seed)`), sem acoplar o modulo ao `random`.

- **Objetivo:** gravar acoes/entradas por frame mais a seed e reproduzi-las.
- **Usos:** reproduzir bugs, testes de regressao visual e de logica, demos
  de attract mode, benchmarks com o mesmo caminho de camera.
- **Ganhos:** o PCSX2 se mostrou deterministico (mesmos tempos com +-0,01 ms
  em duas execucoes, MEDIDO); com entrada gravada, qualquer cenario de jogo
  vira um teste automatico repetivel, inclusive no PS2 real.
- **Implementacao:** JS (fixed step do `Loop` + `Random.state()`), arquivo
  binario compacto.
- **Esforco:** 3-5 dias.

## 5. Fase 3: desempenho (onde o JS e o gargalo)

### Registro da Fase 3 (2026-10-09)

Dois modulos nativos novos, `meshbuilder` e `voxel` (que usa a API C do
`meshbuilder`), mais uma micro-otimizacao em `model3d.c` (cores das malhas
arredondadas sem `lroundf`, ~10% da criacao de malhas). O spike de 2-3 dias
sugerido abaixo nao foi feito como etapa separada: o modulo foi
implementado direto, medido contra os numeros JS do estudo de viabilidade.

Validacao:

- testes host (Docker `js-tests`, ASan/UBSan): `meshbuilder_test.js` (24) e
  `voxel_test.js` (42), dois runtimes cada, com o resto da suite verde;
- PCSX2 2.8.2: os dois testes passam; `bin/voxel_example.js` roda a 60 FPS
  (mundo 96x48x96 gerado com cavernas, voo com `Input`, cavar/colocar com
  raycast); winding (CULL_BACK), AO e camadas conferidos em screenshot;
- PS2 real: **pendente**.

Numeros MEDIDOS no PCSX2 (mundo 64x48x64, chunks 16^3, 32 chunks com
faces; `Date.now()` e contador de ciclos do EE):

| Medida | Valor | JS no estudo |
|---|---|---|
| `generate` 64x48x64 (colinas) / com cavernas | 27 / 75 ms | 3,9 s so uma coluna 16x16x64 com cavernas |
| Chunk 16^3 com faces, naive + AO: geracao das faces | ~6,5 ms | ~80 ms (sem AO) |
| O mesmo chunk: criacao das malhas Model3D | ~10 ms | ~6 ms (`fromGeometry`) |
| Editar um bloco + `rebuild` do chunk | 9 ms | ~80 ms |
| Greedy: 4x menos faces (26.566 -> 6.659) | ~10 ms por chunk | ~630 ms |
| `draw` da visao geral (53k triangulos, 32 malhas), CPU | 6,0 ms | - |
| Memoria das malhas | ~70 B por face | 90-138 B por quad |
| `MeshBuilder`: 4.000 caixas (48k triangulos) em 3 malhas | 210 ms | - |

Leitura: a geracao de faces em C ficou ~12x mais rapida que o JS; o chunk
completo, ~5x, porque a criacao das malhas no `model3d` (validacao, plano
de lotes indexados para o VU1, copia das streams) passou a ser ~60% do
custo. Otimizacoes feitas no caminho: copia local do chunk com borda (sem
bounds check por vizinho), caminho "unchecked" no builder, weld de vertices
iguais no `build()` (menos vertices: draw 7,5 -> 6,0 ms) e nada de
`memcpy`/`memcmp` pequenos em lacos quentes (nao sao inline no EE). O
criterio de saida (chunk completo <= 15 ms no PS2 real) fica no limite no
PCSX2 e precisa de medicao no console.

### 5.1 `voxel`: mundo de blocos nativo (VALIDAR NO HW)

> **Implementado** em `src/modules/voxel/`. API final: `new
> Voxel.World({size, chunk})`, `setMaterial(type, {solid, visible, color,
> tile})` (cor ou tile por grupo de face top/bottom/side), `setStyle({meshing
> "naive" | "greedy", ambientOcclusion, bakedLight, shading, atlas,
> tileSize})`, `get`/`set`/`fill`, `read`/`write` (regioes em Uint8Array,
> para o SaveGame), `generate({seed, baseHeight, amplitude, frequency,
> octaves, top, filler, stone, fillerDepth, caves, caveFrequency, water,
> waterLevel})` (ruido gradiente fBm; cavernas em tunel com o ruido 3D numa
> grade grossa interpolada), `surface`, `rebuild(budgetMs, x, y, z)` (chunks
> sujos mais proximos primeiro; bordas marcam vizinhos, inclusive por AO),
> `stats` (com `lastMeshMs`/`lastBuildMs`), `raycast` (DDA; aceita o `Ray`
> do Camera3D), `moveBox` (AABB por eixo Y, X, Z com `onGround`),
> `boxSolid`, `draw(camera, cull, lights, stats | null, distance)` (culling
> por distancia e frustum, passe compartilhado, totais no
> `Render3D.frameStats()`), `clearMeshes`, `dispose`. Diferencas do esboco:
> sem biomas nem `collide` com velocidade (o `moveBox` cobre o caso); agua
> e um tipo comum (sem translucidez: o `render3d` nao tem blending);
> chunks que envolvem a camera ainda caem no clipper em C (a doc recomenda
> `chunk: 8` em primeira pessoa).

- **Objetivo:** armazenamento de blocos, geracao de terreno, meshing,
  raycast, colisao e desenho de chunks em C, com o jogo em JS.
- **Usos:** jogos tipo Minecraft, construcao/destruicao de terreno,
  puzzles em grade 3D, niveis em blocos estilo "voxel art", mapas
  destrutiveis em jogos de acao.
- **Ganhos:**

  | Sistema | Hoje em JS (MEDIDO) | Com o modulo (ESTIMADO) | Efeito |
  |---|---|---|---|
  | Meshing ingenuo de chunk 16^3 | ~80 ms (+ ~6 ms `fromGeometry`) | ~1-5 ms + criacao | Edicao de bloco cai de ~5 frames para < 1 frame |
  | Greedy meshing 16^3 | ~630 ms | poucos ms | 5x menos triangulos (MEDIDO), frames 2,5-3x mais rapidos (MEDIDO) |
  | Ruido 3D (cavernas) | ~237 us/amostra; 3,9 s por coluna 16x16x64 | ~3,7 us/amostra (C `Noise`, documentado) | Cavernas e streaming de mundo viaveis |
  | Mundo de 1M blocos (geracao + meshing) | ~15 s | ~1-2 s | Loading curto |
  | Blocos no heap JS | contam no `jsLimit` (14,35 MB, MEDIDO) | memoria nativa | Mais espaco para o jogo em JS |
  | Desenho de chunks | ~10 us por item de `Batch`, ~22-30 us por `draw` JS (MEDIDO) | culling e lista em C | Menos overhead com chunks pequenos |
  | Chunks perto da camera (clipper em C) | ~5 ms por 16^3 (MEDIDO) | sub-malhas 8^3 e por direcao de face puladas antes do clipper | Reducao parcial; correcao completa e extensao do `render3d` |

- **O que nao resolve:** limites reais de GS/VU1 (desconhecidos sem
  hardware), ausencia de alpha blending (agua translucida), clipper em C
  sem extensao do `render3d`.
- **API esboco:**
  ```ts
  const world = new Voxel.World({ sizeX: 128, sizeY: 64, sizeZ: 128, chunk: [16, 16, 16], section: [8, 8, 8] });
  world.setMaterials([{ tile: [0, 1, 2] }, ...], atlas);      // faces por tipo de bloco
  world.generate({ seed: "ilha-7", caves: true, biomes: true });
  world.set(x, y, z, Voxel.AIR);                             // marca chunks sujos (e vizinhos na borda)
  world.rebuildDirty(4);                                      // remesh com orcamento de 4 ms por frame
  const hit = world.raycast(eye, dir, 6);                     // DDA: bloco e face
  world.collide(aabb, velocity, dt);                          // AABB vs grade, flags de chao/parede
  world.draw(camera, { distance: 48, lights });               // culling + Batch nativo
  const bytes = world.regionData(rx, rz);                     // para savegame/gzip
  ```
- **Implementacao:** nativo, em duas etapas:
  1. meshing e geracao devolvendo TypedArrays para `Mesh.fromGeometry`
     (zero mudanca no engine);
  2. malhas e desenho proprios via `athena_mesh3d_create` e
     `athena_render3d_draw_mesh` (ja exportados), ou via o wrapper da
     secao 3.4.
- **Esforco:** 2-4 semanas (inclui testes host, build Docker, PCSX2 e PS2 real).
- **Criterio de saida:** chunk 16^3 completo (meshing + criacao) <= 15 ms no
  PS2 real; coluna 16x16x64 com cavernas <= 30 ms; streaming sem cair
  abaixo de 30 fps.
- **Validacao recomendada antes do modulo completo:** spike de 2-3 dias so
  com o meshing ingenuo em C, medido com `docs/minecraft-feasibility/benchmarks/voxel_bench.js`
  lado a lado com a versao JS. Seguir se o ganho for >= 10x.

### 5.2 `meshbuilder`: geracao de geometria nativa (VALIDAR NO HW)

> **Implementado** em `src/modules/meshbuilder/` (nativo, com API C para
> outros modulos). `new MeshBuilder.Builder()` com estado de caneta
> (`color`, `transform`, `uvRect`, `uvTile`), `vertex`/`triangle`, `quad`,
> `box`, `sphere`, `cylinder`, `plane`, `heightmap` (normais por diferenca
> central, cor por altura), `merge(mesh | instance)` (static batching),
> `build(material)` -> `Model3D.Mesh[]` (divide no limite de 65.532
> indices/vertices e solda vertices iguais), contadores e `dispose`. Sem
> extrusao e sem greedy generico nesta versao (o greedy vive no `voxel`).

- **Objetivo:** construir e combinar geometria em C: primitivas (caixa,
  esfera, cilindro, grade, quad), extrusao, merge de malhas estaticas e
  greedy em grades genericas.
- **Usos:** cenario procedural, terreno por heightmap, prototipos sem
  modelos, **static batching** de props (juntar muitas pedras/arvores numa
  malha), malhas de depuracao, base do `voxel` e do `lod`.
- **Ganhos:** gerar arrays em JS custa caro (aritmetica `double` emulada);
  juntar props estaticos reduz o custo por objeto (~10 us por item de
  `Batch`, MEDIDO) e o numero de passes. Uma API declarativa evita erros de
  winding/normais (o estudo precisou validar winding fora do console).
- **API esboco:**
  ```ts
  const b = new MeshBuilder();
  b.box([0,0,0], [1,2,1], { color, uv: atlasTile(3) }).cylinder(...).transform(matrix);
  b.merge(otherMesh, matrix);                     // static batching
  b.heightmap(heights, w, h, { scale, colorBy: "height" });
  const mesh = b.build({ shading: Model3D.DIFFUSE });   // varias malhas se passar de 10.922 quads
  ```
- **Implementacao:** nativo; saida em TypedArrays ou malha direta.
- **Esforco:** 1,5-2 semanas.

## 6. Fase 4: sistemas de jogo 3D

### 6.1 `lod`: nivel de detalhe e visibilidade (PROPOSTO)

- **Objetivo:** grupos de LOD por distancia, distancia maxima de desenho
  com fog automatico e culling por celulas de grade.
- **Usos:** florestas, cidades, mundos abertos, multidoes.
- **Ganhos:** o EE submete ~80 mil triangulos em ~12,5 ms no PCSX2 (MEDIDO)
  e o VU1/GS real sao desconhecidos; trocar malhas distantes por versoes
  simples e cortar o que esta alem da fog reduz triangulos e chamadas de
  draw sem codigo por objeto em JS.
- **API esboco:** `new LOD.Group(node, [{ mesh: high, until: 15 }, { mesh: low, until: 60 }])`,
  `LOD.setCamera(camera)`, `LOD.setDrawDistance(80, { fog: lights })`.
- **Implementacao:** nativo, como sistema do `Loop`, atuando sobre `Scene3D.Node.visible`/malha.
- **Esforco:** 1 semana.

### 6.2 `audio3d`: som posicional (PROPOSTO)

- **Objetivo:** fontes sonoras no mundo e um ouvinte (camera).
- **Usos:** passos, motores, ambiente (rio, vento), tiros, monstros fora
  da tela.
- **Ganhos:** hoje o jogo calcula volume e pan em JS a cada frame para cada
  som; o modulo faz atenuacao por distancia, pan pelo angulo e prioridade de
  canais em C, num unico sistema do `Loop`.
- **API esboco:** `Audio3D.setListener(camera)`, `new Audio3D.Source(sound, { node, minDistance, maxDistance, loop })`.
- **Implementacao:** nativo sobre `sound` (volume/pan por canal).
- **Esforco:** 1 semana.

### 6.3 `triggers3d`: volumes de gatilho (PROPOSTO)

- **Objetivo:** caixas e esferas que disparam `onEnter`/`onExit`/`onStay`.
- **Usos:** checkpoints, portas, cutscenes, zonas de dano, troca de musica,
  carregamento de areas.
- **Ganhos:** substitui verificacoes manuais por frame; filtra por camadas;
  integra com `Collision3D.Character` e `Physics3D`.
- **Implementacao:** JS ou nativo leve (grade espacial).
- **Esforco:** 3-5 dias.

### 6.4 `nav`: navegacao e IA (PROPOSTO)

- **Objetivo:** pathfinding A* em grade/navgrid e steering (seek, arrive,
  flee, evitar obstaculos).
- **Usos:** inimigos, NPCs, mobs de jogo voxel, RTS simples.
- **Ganhos:** A* em JS no EE e caro (objetos e aritmetica `double`, MEDIDO
  ~100 us so para criar um objeto pequeno e iterar com `for..in`); em C, com
  heap binario e arrays fixos, varios agentes por frame ficam viaveis.
- **API esboco:** `const grid = new Nav.Grid(w, h, cellSize)`, `grid.setBlocked(...)`,
  `grid.findPath(from, to, out)`, `new Nav.Agent(node, { speed, radius })`.
- **Implementacao:** nativo; pode usar o `voxel.World` como grade.
- **Esforco:** 1,5-2 semanas.

### 6.5 `sky`: ceu e ambiente (PROPOSTO)

- **Objetivo:** skybox ou ceu em gradiente, presets de fog e ciclo de dia.
- **Usos:** qualquer cena externa; dia/noite em jogos de sobrevivencia.
- **Ganhos:** padroniza o que hoje e montado a mao (clear color, fog do
  `Lights`, malha de ceu). Dia/noite em malhas `UNLIT` exige um multiplicador
  de cor por draw, porque `baseColor` e assado na criacao (VERIFICADO em
  `model3d.c:280-281`); essa parte e uma extensao pequena do `render3d`.
- **Esforco:** 1 semana (+ extensao do `render3d` para o tint).

## 7. Fase 5: ferramentas e conteudo

### 7.1 `dev`: live reload (PROPOSTO)

- **Objetivo:** observar o `mtime` dos scripts e assets em `host:` e
  reiniciar com `std.reload()` (ja existe), opcionalmente preservando estado.
- **Usos:** iterar no PCSX2 sem fechar e abrir o emulador.
- **Ganhos:** encurta o ciclo editar -> ver de minutos para segundos.
- **Implementacao:** JS (polling barato a cada N frames, so em build de desenvolvimento).
- **Esforco:** 3-5 dias.

### 7.2 `bench`: executor de benchmarks (PROPOSTO)

- **Objetivo:** generalizar o executor por tarefas do `voxel_bench.js`
  (aquecimento, N frames medidos, media/p95, `results.json` regravado a cada tarefa).
- **Usos:** A/B de otimizacoes, comparacao PCSX2 x PS2 real, regressao de desempenho em CI.
- **Ganhos:** benchmarks consistentes e comparaveis entre modulos; com
  `replay`, cenas reais viram benchmarks.
- **Esforco:** 3-5 dias.

### 7.3 `ui`: widgets de HUD e menus (PROPOSTO)

- **Objetivo:** listas, botoes, barras, grades de inventario, dialogos,
  com foco por d-pad, sobre `font`/`draw`/`sprite`.
- **Usos:** menus, HUD, inventario, opcoes.
- **Ganhos:** remove a parte mais repetitiva de todo jogo; navegacao por
  controle consistente.
- **Esforco:** 1,5-2 semanas.

### 7.4 CLI de assets (em `tools/`, nao e modulo) (PROPOSTO)

- **Objetivo:** validar e pre-processar glTF/OBJ num formato binario pronto
  para `Mesh.fromGeometry`, empacotar atlas e checar orcamentos.
- **Usos:** pipeline de arte; alertas antes de rodar no console.
- **Ganhos:** carga quase so de I/O (`fromGeometry` ~11 us/quad, MEDIDO) em
  vez de parse em runtime; avisos de limites (10.922 quads por malha,
  texturas potencia de 2 ate 512, UV <= 16, VRAM livre de ~1,3 MB a 640x448,
  MEDIDO) antes de chegar ao PS2.
- **Esforco:** 1-1,5 semana.

## 8. Resumo: ganhos x esforco

| Modulo | Fase | Tipo | Esforco | Ganho principal |
|---|---|---|---|---|
| `debug3d` | 1 | nativo | 1-1,5 sem | Ver geometria, colisao e culling em 3D |
| `profiler` | 1 | nativo | 1 sem | Medir sem distorcer; expor o clipper em C |
| picking/projecao | 1 | extensao nativa | 1 sem | Selecao, mira, UI sobre objetos |
| ajustes de ergonomia | 1 | extensao | 2-3 dias | -80% no custo do `draw` sem `out`; wrapper de malha nativa |
| `input` | 2 | JS/nativo | 1 sem | Controles padronizados e rebinding |
| `assets3d` | 2 | JS | 1 sem | Sem vazamentos; carga em segundo plano |
| `savegame` | 2 | JS | 3-5 dias | Saves seguros, versionados e comprimidos |
| `replay` | 2 | JS | 3-5 dias | Bugs e testes reproduziveis |
| `voxel` | 3 | nativo | 2-4 sem | 10-50x no meshing; cavernas e streaming viaveis |
| `meshbuilder` | 3 | nativo | 1,5-2 sem | Geometria procedural e static batching |
| `lod` | 4 | nativo | 1 sem | Menos triangulos e draws em cenas grandes |
| `audio3d` | 4 | nativo | 1 sem | Som posicional sem JS por frame |
| `triggers3d` | 4 | JS/nativo | 3-5 dias | Gatilhos de area declarativos |
| `nav` | 4 | nativo | 1,5-2 sem | IA com pathfinding viavel no EE |
| `sky` | 4 | nativo + extensao | 1 sem | Ceu, fog e dia/noite padronizados |
| `dev` | 5 | JS | 3-5 dias | Ciclo de iteracao em segundos |
| `bench` | 5 | JS | 3-5 dias | Desempenho mensuravel e comparavel |
| `ui` | 5 | JS | 1,5-2 sem | HUD e menus prontos |
| CLI de assets | 5 | ferramenta | 1-1,5 sem | Carga rapida e erros antes do console |

## 9. Regras para todos os modulos

- Seguir `NEW_MODULE_DIRECTIVES.md` (estrutura, registro, `.d.ts`, testes host).
- Opt-in (`"default": false`) para nao aumentar o `core` de quem nao usa.
- Medir no PCSX2 **e** no PS2 real antes de marcar FEITO; registrar numeros
  com a legenda MEDIDO/ESTIMADO, como em `3D_IMPROVEMENTS.md`.
- Nada de alocacao por frame em caminhos quentes; reutilizar objetos de
  saida (`out`), como ja fazem `Render3D` e `Model3D`.
- Recursos nativos com `dispose()` explicito e finalizer de seguranca.
