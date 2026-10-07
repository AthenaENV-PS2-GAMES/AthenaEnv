# CameraRig3D

Controllers nativos de `Camera3D`. O script configura o rig e repassa a
entrada (`rotate`, `zoom`); seguir, orbitar e suavizar rodam em C a cada
quadro. Ative com `node tools/modules.js configure --modules=...,scene3d,camerarig3d`.

```js
const rig = new CameraRig3D.Follow(camera, hero)
    .setOffset(0, 2, 6)          // atrás e acima, no espaço local do herói
    .setLookOffset(0, 1, 0)
    .setSharpness(6, 10);        // suavização em 1/s; 0 = rígido
scene.attachLoop();
CameraRig3D.attachLoop();        // depois do update da cena

const orbit = new CameraRig3D.Orbit(camera, null).setCenter(0, 1, 0)
    .setLimits(-1, 1, 2, 20);
orbit.autoRotate = .5;           // rad/s
Loop.run({ update() { orbit.rotate(stickX * .05, stickY * .05); } });
```

## Contrato

- **Follow:** olho = matriz de mundo do alvo × offset (local, padrão `0, 2, 6`;
  gira e escala com o alvo) ou posição do alvo + offset (`setOffset(x, y, z,
  false)`). Mira = matriz de mundo × `lookOffset`.
- **Orbit:** centro = posição de mundo do alvo + `setCenter()`, ou o ponto fixo
  sem alvo. Yaw em torno de +Y (0 fica em +Z olhando para −Z), pitch positivo
  para cima, `distance` padrão 6. `rotate()` e `zoom()` respeitam `setLimits()`
  (pitch em −1,56..1,56 rad; padrão −1,5..1,5 e distância 0,1..1e6).
  `autoRotate` soma yaw por segundo.
- **Suavização:** exponencial e independente da taxa de quadros. `snap()` faz a
  próxima atualização saltar direto ao destino; a primeira atualização sempre salta.
- **Ordem:** `CameraRig3D.attachLoop()` registra um único sistema `POST_UPDATE`
  com `LOOP_PRIORITY` 50, depois do `Scene3D.attachLoop()` (0) e do
  `Animation3D` (−100): o rig lê a matriz de mundo já atualizada. Um alvo
  desatualizado faz o rig pular aquele quadro. Sem Loop: `rig.update(dt)` ou
  `CameraRig3D.update(dt)`.
- **Ownership:** o rig retém o alvo e a câmera; a câmera continua válida mesmo
  após `camera.dispose()`, até o rig ser descartado. Em C, o chamador mantém a
  câmera viva (`release_camera` NULL).
- **Câmera:** o rig escreve posição e mira juntas com
  `athena_camera3d_set_view()`, uma validação e uma view por quadro, sem estado
  intermediário degenerado.

## Desempenho (PCSX2)

Follow escrito em JS: 43 µs por quadro; `rig.update()`: 12,3 µs; com
`attachLoop()`, nenhuma chamada JS.
[Medição](benchmarks/3d-camera-rig-2026-10-06.json).
