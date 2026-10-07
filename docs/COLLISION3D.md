# Collision3D

Colisão 3D leve em C, para fases e personagens: não é um solver de corpos
rígidos (Box2D cobre o 2D; ODE segue como trilha futura). Ative com
`node tools/modules.js configure --modules=...,collision3d` (depende de
`scene3d` e `loop`).

```js
const level = GLTF3D.load("models/level.glb");
scene.root.add(level.root);
const world = new Collision3D.World();
world.addNode(level.root);                       // a fase inteira, um shape

const player = new Collision3D.Character(world, { radius: .4, height: 1.8, position: [0, 1, 0] });
player.bind(heroNode);                            // o nó segue os pés
Collision3D.attachLoop(); scene.attachLoop();

Loop.run({
    update() {
        player.vx = stickX * 4; player.vz = stickY * 4;
        if (pad.justPressed(Gamepad.CROSS) && player.onGround) player.vy = 5;
    },
});
```

## World

- **Shapes:** `addNode(node)` (todas as malhas visíveis da subárvore, com os
  transforms dos ancestrais compostos, sem precisar de `scene.update()`),
  `addMesh(mesh, matrix?)`, `addBox(min, max)` e `addTriangles(Float32Array)`
  (9 floats por triângulo, já em mundo). Cada chamada devolve um id; os
  triângulos são copiados. `remove(id)`, `setLayer(id, layer)`. Malhas com
  skin ou morph entram na pose base. Até 262.144 triângulos.
- **Faces:** triângulos olham para o lado anti-horário. Personagens e
  `sphereCast` colidem só com faces da frente; raios acertam os dois lados, com
  a normal virada para a origem.
- **Árvore:** BVH com divisão SAH (12 bins) e bounds por triângulo,
  reconstruída pela próxima consulta depois de qualquer mudança. Triângulos
  grandes (piso, paredes) ficam isolados perto da raiz.
- **Consultas:** `raycast(origin, direction, maxDistance, {mask}, out?)` e
  `sphereCast(center, radius, direction, maxDistance, ...)` devolvem
  `{distance, x, y, z, nx, ny, nz, shape, triangle}` ou `null`; `out` é
  preenchido e devolvido. `raycastMany(origins, directions, maxDistance, out)`
  faz n raios numa chamada e escreve `[distance, nx, ny, nz]` por raio
  (`-1` sem acerto). `overlapSphere(center, radius)` devolve os ids dos shapes
  a até `radius` (até 64).
- **Layers e masks:** flags de 32 bits (padrão: layer 1, mask -1).

## Character

Um elipsoide em pé (`radius` em volta, `height` de altura), posicionado pelos
pés, movido por collide-and-slide (Fauerby, "Improved Collision detection and
Response") em espaço de elipsoide: varre o movimento inteiro, então não
atravessa paredes finas em nenhuma velocidade. Cima é +Y.

- **Opções:** `radius` (0.4), `height` (1.8), `stepHeight` (0.3), `maxSlope`
  em graus (45), `gravity` ([0, -9.81, 0]), `mask` (-1), `position`.
- **Chão e paredes:** faces até `maxSlope` são chão — parado nelas, o
  personagem não escorrega; faces mais inclinadas bloqueiam como paredes.
  Arestas até `stepHeight` acima dos pés são subidas (o fundo arredondado
  desliza sobre elas como numa rampa); acima disso, bloqueiam. Andando para
  baixo em rampas e degraus, o personagem continua no chão (snap).
- **Estado:** `x`, `y`, `z` (pés), `vx`, `vy`, `vz`, `onGround`, `hitWall`,
  `hitCeiling`, `groundNormal`. `vy` zera ao pousar e no teto.
- **Movimento:** `step(dt)` aplica gravidade e move pela velocidade;
  `move(dx, dy, dz)` move com colisão, sem gravidade; `setPosition()`
  teleporta. `Collision3D.attachLoop()` registra um sistema POST_UPDATE
  (prioridade -50: depois do `update` do jogo e do Animation3D, antes do
  Scene3D) que passa todos os personagens ativos; `Collision3D.step(dt)` faz o
  mesmo manualmente.
- **Nós:** `bind(node)` copia a posição dos pés para o nó a cada movimento.
- **Tempo de vida:** personagens ficam ativos até `dispose()`, mesmo sem
  variável que os segure, e mantêm o seu World vivo.

## Desempenho (PCSX2)

Nível de 122 triângulos ([medição](benchmarks/collision3d-2026-10-06.json)):

| Operação | Custo |
|---|---|
| Passo de um personagem andando/pulando | ~39 µs (64 personagens: 2,5 ms) |
| `raycast()` a partir do JS, argumentos reaproveitados | ~35 µs |
| Um raio dentro de `raycastMany()` | ~17 µs |

Crie os vetores de consulta uma vez (`Float32Array`) e prefira
`raycastMany()` para muitos raios: arrays criados a cada chamada, com
matemática `double` (emulada no EE), custavam mais que a própria consulta
(~130 µs por raio).

## Limitações

Personagens não colidem entre si nem empurram objetos; shapes são estáticos
(mova uma plataforma com `remove()` + `add*()`, que reconstrói a árvore).
Sem corpos dinâmicos, juntas ou atrito: essa é a trilha do Physics3D/ODE.

`bin/collision3d_example.js` (`--cfg=collision3d_example.ini`) monta um nível
com degraus e rampa e mede 1, 16 e 64 personagens; `tests/host/collision3d_test.c`
e `bin/tests/collision3d_test.js` cobrem consultas, degraus, rampas,
paredes, teto, pulo e túnel.
