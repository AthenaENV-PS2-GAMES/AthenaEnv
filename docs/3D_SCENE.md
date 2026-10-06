# Scene3D — grafo de transforms nativo (fase 3)

`scene3d` 1.0 é o primeiro incremento da fase 3 do
[roadmap](../portingDocs/3D_MIGRATION_ROADMAP.md): hierarquia nativa com ciclos
rejeitados, dirty flags, bounds por subárvore, culling hierárquico, fila de
desenho ordenada por pipeline e sistema opcional do `Loop`. É um módulo
opcional; `render3d` não depende dele. **Testes host C e QuickJS passam; as sete
etapas C foram aceitas pelo usuário no PCSX2 em 05/10/2026 ([registro](validation/3d-scene-2026-10-05.json)); a execução QuickJS está pendente.**

## Executar as cenas

```sh
node tools/modules.js configure --modules=screen,loop,scene3d,draw,tilemap,system,timer,usbmass
# Dentro da imagem PS2SDK no WSL/Docker:
sh tools/build_3d.sh
```

- JavaScript: `bin/athena_3d_js.elf --cfg=3d_scene.ini` ([bin/3d_scene.js](../bin/3d_scene.js)).
- C: `bin/athena_3d_scene_native.elf` ([samples/native/3d_scene/main.c](../samples/native/3d_scene/main.c)).

Execute a partir de `bin/`, preservando `models/lit_cube.glb`. Sete etapas de
180 frames; a cena termina sozinha. O script só altera transforms de nós;
matrizes mundiais, bounds, culling e submissão acontecem em C.

| Etapa | Verificar na imagem |
| --- | --- |
| 0 — hierarchy | Sol amarelo girando; planeta menor orbita e gira; o triângulo colorido (lua) acompanha o planeta. |
| 1 — reparent | A cada 60 frames a lua troca de pai entre sol e planeta, mantendo o transform local: muda de órbita sem corrupção. |
| 2 — visibility | Planeta e lua somem e reaparecem juntos a cada 45 frames; o sol continua visível. |
| 3 — subtree-cull | O sistema inteiro varre a tela na horizontal e sai pelas bordas; `culledSubtrees` aumenta fora da tela. |
| 4 — many-nodes | Grade 8×8 de cubos sob 8 linhas; linhas alternadas giram em sentidos opostos e o conjunto gira. |
| 5 — loop-system | Igual à etapa 0, mas atualizada pelo sistema nativo `POST_UPDATE` do Loop. |
| 6 — recreate | Igual à etapa 0, com cena, nós e malhas descartados e recriados a cada 30 frames. |

Os logs imprimem `visited`, `worldUpdates`, `queued`, `submitted`, `culled`,
`culledSubtrees`, `triangles` e `geometryBytes` a cada 60 frames e, ao fim de
cada etapa, a média de update/draw dos últimos 120 frames (ms em C; unidades do
`Timer` em JS). Na etapa 5 o update aparece como zero no JS porque ocorre no
sistema do Loop. Esses números são diagnósticos para o benchmark de muitos
objetos; não comparam com o legado.

## API

```js
const scene = new Scene3D.Scene();
const root = scene.root;              // novo handle do nó raiz
const hero = new Scene3D.Node(mesh)   // retém a malha
    .setPosition(0, 0, -5).setRotationEuler(0, Math.PI / 4, 0);
root.add(hero); root.dispose();
scene.attachLoop();                   // ou scene.update() manualmente
Loop.run({
    update(dt) { hero.setPosition(x, 0, -5); },
    draw() { scene.draw(camera, Render3D.CULL_BACK, lights); },
});
```

C usa as mesmas operações: `athena_node3d_*`, `athena_scene3d_update()`,
`athena_scene3d_draw()` e `athena_scene3d_attach_loop()`. Um jogo C com loop
próprio chama `athena_loop_systems_run()` ou `athena_scene3d_update()`.

### Contratos

- **Transform:** `world = parent.world × local`, column-major como `Matrix4`;
  local é TRS com Euler `Rz·Ry·Rx` (radianos) ou quaternion xyzw normalizado.
- **Nó e instância:** o nó é a instância da malha no grafo. `Model3D.Instance`
  continua para uso plano e `Render3D.Batch`; o nó não usa o TRS de uma instância.
- **Ownership:** o pai retém os filhos e o nó retém a malha. O filho só aponta
  para o pai. `add()` reparenta preservando o local; ciclos (`a.add(a)`, ancestral
  como filho), raiz de cena como filho e profundidade acima de `MAX_DEPTH` (64
  níveis, raiz incluída) lançam `RangeError` sem alterar o grafo. Cada handle JS
  possui uma referência; `getParent()`, `getChild()` e `scene.root` criam novos
  handles, não objetos idênticos. `dispose()` é idempotente.
- **Dirty flags:** setters marcam o nó e propagam um bit de subárvore até o
  primeiro ancestral já marcado. `update()` só desce por caminhos marcados e
  recalcula matrizes apenas onde o local ou o pai mudou; bounds são refeitos ao
  longo desse caminho. Cena limpa: `visitedNodes === 0`.
- **Sem atualização implícita:** `draw()`, `getWorldTransform()` e
  `getWorldBounds()` lançam `InternalError` com a cena desatualizada ou com o nó
  fora de uma cena. `getLocalTransform()` está sempre atual.
- **Visibilidade:** nós ocultos e seus descendentes não são desenhados nem
  entram nos bounds; alternar visibilidade não recalcula matrizes.
- **Overflow:** transforms não finitos fazem `update()` lançar `RangeError` e a
  cena continua desatualizada até ser corrigida.
- **Draw:** subárvores com AABB mundial fora do frustum são rejeitadas sem
  visitar os filhos; suas malhas contam em `submittedObjects` e `culledObjects`.
  As malhas restantes entram numa fila reutilizável, ordenada de forma estável
  por pipeline (unlit, diffuse, texturizado), e são enviadas por
  `athena_render3d_draw_mesh()` com o mesmo recorte e culling por objeto do
  Render3D. Uma subárvore cujo AABB mundial fica inteiramente dentro do frustum
  não repete o teste nos descendentes, e suas malhas usam
  `athena_render3d_draw_mesh_contained()`, sem o teste por objeto. A coleta termina antes da primeira submissão, então falta de memória
  da fila não deixa um frame parcial. Erros do Render3D interrompem o draw.
- **Loop:** `attachLoop(priority)` registra um sistema nativo em `POST_UPDATE`
  que retém a cena; `detachLoop()`, `dispose()` e o finalizer da cena o removem.
  A limpeza do runtime QuickJS remove os sistemas registrados por aquele
  contexto; anexos feitos pela API C permanecem.

## Validação executada

- `tests/host/scene3d_test.c`: composição TRS/Euler, bounds afins, visitas e
  recomputações por dirty path, ciclos/raiz/profundidade (inclusive ao mover
  uma cadeia), reparent, visibilidade, culling de subárvore, overflow,
  consultas fora da cena, sistema do Loop e liberação única do grafo. Executado
  com UBSan e, em 32 bits, com ASan/LeakSanitizer.
- `bin/tests/scene3d_test.js`: mesmas regras pelo binding, erros tipados,
  handles independentes, cena anexada ao Loop durante o teardown e repetição
  em dois runtimes novos.
- Testes host 3D anteriores continuam passando após `athena_render3d_draw()`
  delegar para `athena_render3d_draw_mesh()`.

Scene3D × Batch com o mesmo grid: `samples/native/3d_profile` e o
[primeiro perfil](benchmarks/3d-profile-2026-10-05.json), anterior à conversão
para `float`.

Após a conversão para `float`, Scene3D desenha 64 cubos difusos em ~1,13 ms
contra ~1,41 ms do Batch, com imagem e contadores iguais; cenas C/JS aceitas
no PCSX2 a 60 FPS. Com o recorte também em `float` (06/10), `many-nodes`
caiu de 20,5 para 1,7 ms de draw. Pendentes: PS2 real, transparência e
identidade de materiais (roadmap, seção 7.3). Transparência,
ordenação por textura/material real, consultas espaciais e integração com
animação/física ficam para os próximos incrementos.
