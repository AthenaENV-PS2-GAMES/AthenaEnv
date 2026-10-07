# Particles3D

Partículas 3D nativas desenhadas como quads virados para a câmera
(billboards). Emissão e integração rodam em C; o VU1 monta e projeta cada quad.
O teste de profundidade usa o z-buffer da cena sem escrever nele, então
partículas transparentes não escondem umas às outras. Desenhe depois da cena
opaca. Ative com `node tools/modules.js configure --modules=...,image,particles3d`;
a tela precisa de z-buffer (`mode.zbuffering = true`).

```js
const fire = new Particles3D.Emitter(new Image("spark.png"), {
    capacity: 1000, rate: 400, life: [1, 2], speed: [3, 5],
    direction: [0, 1, 0], spread: .4, gravity: [0, -3, 0], drag: .1,
    size: [.35, .05], color: [Color.new(255, 220, 160, 128), Color.new(255, 60, 20, 0)],
    area: [.6, 0, .6],
}).setPosition(0, 0, -2);
Particles3D.attachLoop();                  // POST_UPDATE
Loop.run({
    draw() {
        scene.draw(camera, Render3D.CULL_BACK, lights);
        fire.draw(camera);                 // returns how many were sent
    },
});
```

## Contrato

- **Opções** (todas opcionais; `configure()` aceita mudanças parciais):
  `capacity` (1..16384, padrão 256), `rate`, `life`, `speed`, `size` (número ou
  `[mín, máx]`; `size` é `[início, fim]` em unidades de mundo), `direction`
  (eixo do cone, normalizado; padrão `[0, 1, 0]`), `spread` (meio ângulo do cone
  em radianos, 0..π; a distribuição é uniforme na calota), `gravity` e `area`
  (`[x, y, z]`), `drag`, `color` (`Color` ou `[início, fim]`, alfa 0..128),
  `rect` (`[u1, v1, u2, v2]` em texels) e `seed`.
- **Recorte:** partículas fora do intervalo near/far são descartadas no EE. Os
  quatro cantos de um billboard têm a mesma profundidade de view, então o teste
  é exato e o VU1 não processa o que não seria visto. No VU1, uma partícula com
  um canto além da guard band (quatro telas) é descartada inteira com o bit ADC;
  as de borda continuam visíveis.
- **Desenho:** dois qwords por partícula (centro, metade do tamanho, cor) vão
  para `vu1/draw_3D_billboards.vcl`, que monta os cantos com os eixos direito e
  cima da câmera, aplica a view-projection (Y invertido e reversed-Z, como o
  Render3D) e escreve a triangle strip. TEST/ZBUF da tela são restaurados
  depois de uma barreira.
- **Imagem:** o emissor segura o handle; uma imagem liberada não desenha nada.
- **C:** `<athena/particles3d.h>`; `athena_emitter3d_draw()` recebe a câmera e
  o `AthenaImage`.

## Desempenho (PCSX2)

Por partícula e por quadro: update 0,29 µs e desenho 0,79 µs, iguais com 1000
e 2000 partículas. [Medição](benchmarks/particles3d-2026-10-06.json);
`bin/particles3d_profile.js` (`--cfg=particles3d_profile.ini`) desenha a fonte
atrás de uma fileira de cubos.
