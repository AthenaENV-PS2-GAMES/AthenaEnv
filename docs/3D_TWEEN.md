# Tween3D

Tweens de objetos 3D nativos avançados em C: `Scene3D.Node`, `Model3D.Instance`
e `Camera3D.Camera`. Mesma semântica do `Tween` em JavaScript, sem código JS
por quadro. Para objetos JavaScript comuns, continue usando `Tween`. Ative com
`node tools/modules.js configure --modules=...,scene3d,tween3d`.

```js
Tween3D.attachLoop();   // PRE_UPDATE, como Tween
async function open() {
    await Tween3D.to(door, {rotation: [0, Math.PI / 2, 0]}, .6, {ease: "outBack"});
    Tween3D.to(lamp, {position: [0, 2.2, 0]}, 1, {ease: "inOutSine", yoyo: true, repeat: Infinity});
}
Tween3D.to(camera, {position: [0, 3, 10], target: [0, 1, 0]}, 2, {ease: "inOutCubic"});
```

## Contrato

- **Props:** `position` e `scale` (Node/Instance), `rotation` (Euler em radianos,
  `Rz·Ry·Rx`, alcançado por slerp no arco curto; curvas com overshoot extrapolam
  o arco), `position` e `target` na câmera. Cada uma com três números (Array ou
  `Float32Array`). Props de outro tipo de alvo lançam `RangeError`.
- **Opções:** `ease` (nomes curtos ou longos do `Ease`, padrão `"outQuad"`;
  curvas implementadas em C em `ease_curves.c`), `delay`, `repeat` (inteiro ou
  `Infinity`), `yoyo`, `overwrite`.
- **Tempo:** os valores iniciais são lidos quando o tween começa (após o delay);
  o último quadro aplica exatamente o valor final (ou o inicial após um ciclo
  yoyo de volta). Passos grandes com `repeat: Infinity` não iteram ciclo a ciclo.
- **Handle:** `active`, `paused`, `progress`, `pause()`, `resume()`,
  `kill(complete)`; `await handle` e `handle.finished` resolvem com `true` ao
  completar e `false` ao ser cancelado (inclusive por `overwrite`).
- **Ownership:** o tween retém o alvo; câmeras continuam válidas após `dispose()`.
  A limpeza do runtime cancela os tweens criados por ele.
- **Loop:** `Tween3D.attachLoop()` registra um sistema `PRE_UPDATE`, então o
  `update` do jogo já vê os valores interpolados e o Scene3D os aplica no
  `POST_UPDATE`. Sem Loop: `Tween3D.advance(dt)`.
- **C:** `<athena/tween3d.h>` (`athena_tween3d_create()` com
  `AthenaTween3DDesc`, callback de fim, kill por alvo ou dono) e
  `<athena/ease_curves.h>`.

## Desempenho (PCSX2)

64 cubos com yoyo infinito: Tween JS em objetos mais um `setPosition` por cubo
custa 8,0 ms por quadro (metade do orçamento a 60 FPS); Tween3D custa 0,11 ms.
[Medição](benchmarks/3d-tween3d-2026-10-06.json).
