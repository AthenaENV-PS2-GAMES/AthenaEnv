# Particles2D

Partículas 2D nativas: emissão, integração e desenho em C, com o quad de cada
partícula montado pelo VU1. O script configura o emissor, move-o e chama
`draw()` uma vez por quadro; nada por partícula roda em JavaScript. Ative com
`node tools/modules.js configure --modules=...,image,particles2d`.

```js
const sparks = new Particles2D.Emitter(new Image("spark.png"), {
    capacity: 800, rate: 400, life: [.6, 1.2], speed: [80, 160],
    angle: -Math.PI / 2, spread: 1, gravity: [0, 200], drag: .5,
    size: [10, 2], color: [Color.new(255, 220, 120, 128), Color.new(255, 40, 0, 0)],
    spin: [-4, 4], area: [16, 4],
});
Particles2D.attachLoop();                 // POST_UPDATE: nasce onde o jogo deixou
Loop.run({
    update() { sparks.setPosition(player.x, player.y); },
    draw() { sparks.draw(); },
});
```

## Contrato

- **Opções** (todas opcionais; `configure()` aceita mudanças parciais):
  `capacity` (1..16384, padrão 256), `rate` (por segundo), `life`, `speed`,
  `size`, `rotation`, `spin` (número ou `[mín, máx]`; `size` é `[início, fim]`),
  `angle`/`spread` (radianos, 0 = +X, π/2 = para baixo), `gravity` e `area`
  (`[x, y]`), `drag` (`v *= 1 / (1 + drag·dt)`), `color` (`Color` ou
  `[início, fim]`, alfa 0..128), `rect` (`[u1, v1, u2, v2]` em texels) e `seed`
  (mesma semente, mesmas partículas).
- **Pool:** `emit(n)` devolve quantas couberam; partículas mortas saem numa
  compactação que preserva a ordem de nascimento, então o desenho vai da mais
  antiga para a mais nova. `active = false` para a emissão contínua sem apagar
  as vivas.
- **Imagem:** o emissor segura o handle; uma imagem liberada não desenha nada.
- **Desenho:** dois qwords por partícula (centro, meia extensão rotacionada, cor)
  vão inline no anel de DMA para `vu1/draw_2D_particles.vcl`, que monta os
  quatro cantos, aplica a view 2D (Camera2D, inclusive rotação) e escreve a
  triangle strip. Seno e cosseno no EE usam uma aproximação rápida
  (erro < 0,002) em vez de `sinf`/`cosf`.
- **C:** `<athena/particles2d.h>`; `athena_emitter2d_draw()` recebe o
  `AthenaImage`.

## Desempenho (PCSX2)

Por partícula e por quadro: simulação 0,27 µs; desenho 0,69 µs sem rotação e
1,07 µs com rotação (o caminho anterior pelo EE custava 0,99 e 3,50 µs).
2000 partículas: 1,65 ms por quadro. Em JavaScript puro, 300 partículas já
custam 21 ms. [Medição](benchmarks/particles2d-2026-10-06.json).
