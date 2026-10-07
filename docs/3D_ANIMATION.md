# Animation3D

Clipes de keyframes amostrados em C e aplicados a nós do Scene3D. O script cria
o clipe e inicia os players; poses não são calculadas em JavaScript a cada
quadro. Ative com `node tools/modules.js configure --modules=...,scene3d,animation3d`.

```js
const times = new Float32Array([0, 1, 2]);
const clip = new Animation3D.Clip([
    {path: "position", times, values: new Float32Array([0,0,0, 0,2,0, 0,0,0])},
    {target: 1, path: "rotation", times: new Float32Array([0, 2]),
     values: new Float32Array([0,0,0,1, 0,1,0,0])},          // xyzw
]);
const player = new Animation3D.Player(clip, [body, arm]);
player.loop = true; player.play();
Animation3D.attachLoop();   // antes de scene.attachLoop() na mesma fase
scene.attachLoop();
```

## Contrato

- **Trilhas:** `target` (índice no array de nós do player, padrão 0), `path`
  (`"position"`, `"rotation"`, `"scale"`, `"weights"`), `times` (`Float32Array`, segundos ≥ 0,
  estritamente crescentes), `values` (3 floats por key; 4 para rotação xyzw; em
  `"weights"`, um por morph target, 1 a 8, `values.length / times.length`) e
  `interpolation` (`"linear"`, padrão, ou `"step"`). Tudo é validado e copiado:
  os arrays podem ser reutilizados. Até 1024 trilhas e 65.536 keys por trilha.
- **Rotação:** keys normalizadas e levadas ao mesmo hemisfério da anterior, então
  o slerp segue o arco curto. Ângulo e `1/sin` de cada segmento são calculados
  na criação: amostrar custa dois `sinf`.
- **Player:** retém o clipe e os nós (também após `dispose()` dos handles).
  Começa parado, `time` 0, `speed` 1, sem loop. `play()`, `pause()`, `stop()`
  (volta a 0, ou ao fim com `speed` negativa). Atribuir `time` busca e aplica a
  pose. `advance(dt)` avança `dt * speed`; sem loop, para no fim e retorna `true`
  naquele passo. O cursor por trilha torna o avanço para frente O(1).
- **Loop:** `Animation3D.attachLoop(priority = LOOP_PRIORITY)` registra um único
  sistema `POST_UPDATE` que avança todos os players; `LOOP_PRIORITY` (−100) roda
  antes de `Scene3D.attachLoop()` (0), então o update da cena vê a pose do
  quadro. Sem Loop: `Animation3D.advance(dt)` ou `player.advance(dt)`. A limpeza
  do runtime QuickJS remove o sistema que ele registrou.
- **C:** `athena_clip3d_create()`, `athena_player3d_*`, `athena_animation3d_advance()`
  e `athena_animation3d_attach_loop()` em `<athena/animation3d.h>`.

## Desempenho (PCSX2)

64 cubos, um player por cubo, clipe de rotação com 13 keys: update de 610 µs em
C e 660 µs em QuickJS, contra 1148 µs girando cada cubo com `setRotationEuler`
em JS. Amostrar custa ~1,7 µs por player.
[Medição](benchmarks/3d-animation-2026-10-06.json).

## Pendente

Import de animações glTF, eventos por tempo, blending entre clipes e skinning
(palette de ossos no VU1), conforme a seção 7.4 do roadmap.
