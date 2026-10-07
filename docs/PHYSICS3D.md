# Physics3D

Corpos rígidos em C: esferas, caixas e cápsulas que caem, quicam, deslizam, rolam e
empilham, entre si e contra a fase estática de um `Collision3D.World`. Ative
com `node tools/modules.js configure --modules=...,physics3d` (traz
`collision3d`, `scene3d` e `loop`).

```js
const level = new Collision3D.World();
level.addNode(levelNode);                          // a fase: triângulos estáticos

const physics = new Physics3D.World(level).attachLoop();
const crate = physics.addBox({ halfExtents: [.5, .5, .5], mass: 2, position: [0, 5, 0] });
const ball = physics.addSphere({ radius: .3, position: [1, 6, 0], restitution: .6 });
crate.bind(crateNode); ball.bind(ballNode);        // posição e rotação seguem o corpo
scene.attachLoop();

// Um chute:
ball.applyImpulse(0, 3, -2);
```

## World

- `new Physics3D.World(statics?, { gravity, iterations, staticMask })`:
  `statics` é a fase (`Collision3D.World`, ou `null`); gravidade padrão
  `[0, -9.81, 0]`; 8 iterações de velocidade por substep.
- `addSphere(options)` / `addBox(options)` / `addCapsule(options)` criam
  corpos (até 1024 por mundo). A cápsula é um segmento ao longo do y local
  (`halfHeight`, sem as tampas) com raio `radius`: altura total
  2 × (`halfHeight` + `radius`); gire-a com `rotation`.
- `step(dt)` avança em substeps fixos de 1/60 s (até 4 por chamada; um
  quadro mais lento descarta o atraso em vez de acumular). `attachLoop()`
  registra um sistema POST_UPDATE (prioridade -60: antes dos personagens do
  Collision3D e do Scene3D) que passa o mundo com o dt do quadro.
- `bodyCount`, `contactCount` (último substep), `setGravity()`, `dispose()`.

## Body

- **Opções:** `type` (`"dynamic"`, `"kinematic"`, `"static"`), `radius` ou
  `halfExtents`, `mass`, `position`, `rotation` (quaternion), `velocity`,
  `angularVelocity`, `friction` (0.5; média geométrica no par), `restitution`
  (0..1; a maior do par), `rollingFriction` (esferas, 0.02),
  `linearDamping` (0.05), `angularDamping` (0.1), `layer`/`mask`.
- **Tipos:** cinemáticos se movem pela velocidade e empurram os dinâmicos sem
  serem empurrados (plataformas, portas); estáticos não se movem.
- **Estado:** `x`, `y`, `z`, `vx`/`vy`/`vz`, `wx`/`wy`/`wz` (rad/s), `getRotation(out?)`, `sleeping`, `alive`, `type`.
- **Ações:** `setPosition()`, `setRotation()`, `setVelocity()`,
  `setAngularVelocity()`, `applyImpulse(x, y, z, px?, py?, pz?)` (num ponto do
  mundo, padrão o centro), `applyForce()` (só no próximo passo), `wake()`.
- **Nós:** `bind(node)` copia posição e rotação para o nó depois de cada
  passo.
- **Vida:** `remove()` tira o corpo do mundo; `dispose()` também solta o
  handle. Um mundo descartado leva seus corpos.

## Juntas

```js
const link = physics.addBox({ halfExtents: [.15, .25, .15], position: [0, 7.75, 0] });
physics.addBallJoint(link, null, [0, 8, 0]);                 // pendurado no mundo
const door = physics.addBox({ halfExtents: [.8, 1.2, .08], position: [.8, 1.2, 0] });
const hinge = physics.addHingeJoint(door, null, [0, 1.2, 0], [0, 1, 0],
    { lower: -Math.PI / 3, upper: Math.PI / 3 });
hinge.setMotor(1.5, 50);                                      // rad/s, torque máximo
physics.addDistanceJoint(weight, null, [5, 4, 0], [5, 7, 0], { rope: true });
physics.addWeldJoint(bladeA, bladeB, [0, 6, 0]);
```

- **Tipos:** `addBallJoint` (as âncoras ficam juntas, rotação livre),
  `addHingeJoint` (gira só em torno do eixo; `lower`/`upper` em radianos a
  partir da pose de criação; motor por `motorSpeed`/`maxMotorTorque` ou
  `setMotor()`), `addDistanceJoint` (mantém `length`, padrão a distância
  atual; `rope: true` só impede que se afastem) e `addWeldJoint` (mantém
  posição e rotação relativas).
- **Pontos e eixos** em coordenadas do mundo na criação; `b` `null` prende ao
  mundo. Pelo menos um corpo dinâmico; até 1024 juntas por mundo.
- **Comportamento:** corpos ligados não colidem entre si; dormem e acordam
  juntos (a junta une as ilhas); remover um corpo remove suas juntas.
  `joint.angle` dá o ângulo da dobradiça no último passo.
- **Solver:** nas mesmas iterações dos contatos — bloco 3×3 para o ponto,
  linhas angulares (2 na dobradiça, 3 no weld), limite e motor no eixo, uma
  linha na distância —, com Baumgarte e warm starting.

Cena de exemplo: `bin/joints_example.js` (`--cfg=joints_example.ini`), 15
corpos e 14 juntas em ~0,6 ms por passo
([medição](benchmarks/physics3d-joints-2026-10-06.json)).

## Como funciona

Impulsos sequenciais (Catto) por substep: integra velocidades, gera contatos,
resolve, integra posições. Contatos:

- **Fase:** esfera × triângulo (ponto mais próximo), caixa × triângulo (cantos
  atrás do plano cuja projeção cai no triângulo, um contato por canto, e
  cantos do triângulo dentro da caixa); só faces da frente.
- **Cápsulas:** pontos mais próximos do segmento — as duas pontas sempre, e o
  ponto mais próximo quando longe delas (cápsula sobre a quina de uma caixa
  ou de um degrau); cápsulas quase paralelas têm contato nas duas pontas, e
  uma cápsula deitada repousa em dois pontos. Rolam com a mesma resistência
  das esferas ([medição](benchmarks/physics3d-capsules-2026-10-06.json)).
- **Entre corpos:** esfera × esfera, esfera × caixa (ponto mais próximo na
  caixa orientada) e caixa × caixa (eixo separador em 15 eixos; cantos dentro
  da outra caixa para eixos de face, pontos mais próximos das arestas para
  eixos de aresta). Broadphase sort-and-sweep em x.
- **Estabilidade:** contatos especulativos a 2 cm (só impedem a aproximação),
  warm starting (o impulso do passo anterior volta ao contato do mesmo par no
  mesmo ponto), Baumgarte 0.2 com slop de 5 mm, atrito em duas tangentes e
  resistência ao rolamento das esferas.
- **Sono:** corpos ligados por contatos formam ilhas (union-find) e dormem
  juntos depois de 0,5 s lentos; um corpo acordado que encosta acorda a ilha
  sem zerar o tempo, então ela volta a dormir junto. Um corpo dormindo nunca
  fica sob um acordado.

## Solver no VU0

Cada contato é resolvido por um bloco VU0 (macro mode): as velocidades dos
dois corpos ficam em registradores vetoriais nas suas três linhas (duas de
atrito e a normal, com os limites), e só voltam à memória no fim. Os dados de
cada substep são empacotados em quadwords alinhados. Isso deixou o solver
3,6× mais rápido no PS2 (128 corpos: 5,5 → 1,5 ms;
[medição](benchmarks/physics3d-vu0-2026-10-06.json)). Fora do PS2 o mesmo
cálculo roda em C, coberto pelos testes host; `bin/physics3d_check.js`
(`--cfg=physics3d_check.ini`) confere o caminho VU0 no console.

`world.profile` dá o tempo do último `step()` por fase (`collide`, `prepare`,
`solve`, `integrate`, em µs) para medir cenas reais.

## Desempenho (PCSX2)

[Medição](benchmarks/physics3d-vu0-2026-10-06.json) com caixas, esferas e
cápsulas, pilha ainda ativa (2 a 4 s após soltar os corpos; a primeira versão
está em [physics3d](benchmarks/physics3d-2026-10-06.json)):

| Corpos | Contatos | Passo por quadro |
|---|---|---|
| 32 | ~31 | 0,76 ms |
| 128 | ~246 | 6,0 ms |
| 256 | ~688 | 17,9 ms |

Para um quadro de 60 Hz, mantenha algumas dezenas de corpos acordados ao
mesmo tempo; corpos dormindo não custam passo. Menos iterações
(`iterations: 4`) reduzem o custo do solver (não medido), com pilhas menos
firmes.

## Limitações

Só esferas, caixas e cápsulas (sem cilindros ou malhas dinâmicas), sem
molas nas juntas, sem CCD (objetos muito rápidos e finos podem atravessar; o passo de
1/60 s e a margem especulativa cobrem velocidades usuais) e sem colisão com
os personagens do Collision3D. O ODE legado segue como alternativa futura
para juntas e formas complexas.

`bin/physics3d_example.js` (`--cfg=physics3d_example.ini`) solta 32, 128 e 256
corpos num fosso com rampa; `tests/host/physics3d_test.c` e
`bin/tests/physics3d_test.js` cobrem queda livre, repouso, sono, pilha,
quique, atrito, cinemáticos, nós e remoção.
