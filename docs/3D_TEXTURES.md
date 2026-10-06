# Texturas no renderer 3D estático

O incremento de 04/10/2026 implementa UVs, recursos `Model3D.Texture` retidos
pelas malhas e um microprograma VU1 dedicado ao passe opaco com ST/Q.
Iluminação e as nove etapas de texturas foram aceitas visualmente pelo usuário
em C/JS no PCSX2, com logs completos e contadores correspondentes.
O incremento de UVs/texturas está concluído nesse alvo; PS2 real e medições
de desempenho permanecem como acompanhamento.

## Executar as cenas

Na pasta `bin/`, execute `athena_3d_textures_native.elf` ou
`athena_3d_js.elf --cfg=3d_textures.ini`. Preserve `models/textured_cube.glb`,
`models/checker.png`, `3d_textures.js` e a configuração.
Sem argumentos no launcher, use uma pasta separada com `athena.ini` contendo
`default_script=3d_textures.js`. O arquivo `bin/athena.ini` atual foi preservado.

São nove etapas de 180 frames, com indicadores no HUD e logs a cada 60 frames:

| Etapa | O que verificar |
| --- | --- |
| unlit | Dois cubos e um quad, com quadrantes vermelho/verde/azul/amarelo e padrão quadriculado. |
| diffuse | Mesmo padrão modulado por ambiente e uma luz direcional. |
| perspective | Quad inclinado, com W diferente nos vértices; padrão contínuo entre os dois triângulos, sem distorção afim. |
| clip-sweep | Quad cruza planos e muda de profundidade; preservar a parte visível, com UVs contínuas nas interseções. |
| filters | Cubo esquerdo nearest, direito linear; bordas mais suaves no direito. |
| camera-tile-camera | Cubo → TileMap texturizado → outro cubo/câmera → quad; cada passe mantém sua textura e projeção. |
| retained-texture | Textura procedural copiada; handle liberado no frame 90, malhas continuam desenhando. |
| recreate | Malhas, instâncias, lote e texturas liberados a cada 30 frames, depois do draw e antes do flip; recriação no frame seguinte. |
| video-reset | Modo de vídeo reaplicado no frame 90, com desenhos pendentes; no frame seguinte, recursos retidos reenviam os pixels. Um frame de transição pode perder o conteúdo ao trocar os buffers. |

As cenas C e JS têm a mesma geometria, transformações e ordem. Em condições
sem recorte, são 3 objetos, 26 triângulos fonte/saída, 3 chunks e 1.920 bytes
de geometria unlit; diffuse transmite 2.880 bytes. Em camera-tile-camera,
o log mede somente o primeiro cubo: 1 objeto, 12 triângulos, 1 chunk, 864 bytes.
O recorte pode ampliar triângulos/payload. Esses logs não são benchmarks de tempo.

Nos testes C/JS aceitos, clip-sweep produziu 28 triângulos a partir de 26,
com dois recortados e 2.064 bytes de geometria. Os contadores voltaram a
26 triângulos/1.920 bytes ao sair do recorte. A liberação do handle e o
reinício de vídeo preservaram os contadores nos frames posteriores; o usuário
confirmou o resultado visual das nove etapas nos dois runtimes, incluindo
retenção e recriação. O log QuickJS também mostrou zero objetos culled e zero
triângulos rejeitados nos samples reportados. O CRC do ELF informado pelo
emulador foi `6C059702`; não equivale ao SHA-256 registrado do artefato.

## API e ownership

```js
const texture = Model3D.Texture.load('models/checker.png', Model3D.Texture.NEAREST);
const mesh = Model3D.Mesh.fromGeometry({
    positions: new Float32Array([-1,-1,-3, 1,-1,-3, 0,1,-3]),
    texcoords: new Float32Array([0,1, 1,1, .5,0]),
    material: {texture, shading:Model3D.UNLIT}
});
texture.dispose(); // Mesh retém o recurso nativo.
const object = mesh.createInstance();
mesh.dispose(); // Instance retém Mesh.
```

`Texture.fromPixels({width,height,pixels,filter?})` copia `Uint32Array` com
pixels little-endian `0xAABBGGRR`, respeitando subarrays. Alpha é ignorado;
o passe é opaco. `Texture.load(path,filter?)` decodifica sincronicamente imagens
RGB24/RGBA32 usando o decoder existente. Paletas não são suportadas neste corte.
Dimensões devem ser potências de dois entre 1 e 512. Filtro padrão nearest;
linear é opcional. Clamp-to-edge, sem repeat, mipmaps ou transparência.

UVs são pares Float32 finitos em [0,1], um por vértice fonte, com origem no
canto superior esquerdo. A malha copia/expande posições, normais, cores e UVs.
O descriptor de material é copiado; seu recurso de textura é retido.
`dispose()` é idempotente e os finalizers cobrem handles não liberados.
Esses recursos pertencem à thread principal.

Headers C: `athena/model3d.h` e `athena/texture3d.h`. Use
`athena_texture3d_create/load/retain/release`; informe `texture` no material e
`texcoords/texcoord_count` na geometria. As structs de geometria/material/view
foram ampliadas em Model3D 1.2: recompilar consumidores C e usar inicialização
nomeada, ou inicializar materiais com `athena_material3d_default()`.

## Assets e lifetime GS

OBJ importa `vt` completo, invertendo V para a origem superior esquerda;
mapas de material OBJ continuam rejeitados. Passe uma textura explícita.
glTF/GLB importa `TEXCOORD_0` e pode carregar automaticamente a imagem externa
de `baseColorTexture`, relativa ao arquivo do modelo. O sampler precisa declarar
clamp S/T e filtros nearest ou linear iguais para min/mag, sem mipmaps.
Imagens embutidas, data URIs, URI com esquema/percent-encoding, transforms UV,
canais UV diferentes de zero e outros mapas/PBR avançados são rejeitados.
Um override `material.texture` substitui a imagem/sampler do asset, permitindo
usar uma textura compatível com um modelo que contém imagem embutida ou repeat.
Limites de parsing completo e carregamento por jobs continuam no roadmap.

O backend cria a superfície GS sob demanda, faz upload síncrono e mantém
a textura residente com lock até a última referência. Isso troca flexibilidade
de eviction por lifetime simples; recursos demais podem retornar erro de VRAM.
Os pixels copiados permanecem em RAM para re-upload depois de `Screen.setMode()`.
Não há orçamento ou fila assíncrona de upload neste incremento.

Primeiro upload, última liberação residente e troca de modo drenam comandos
pendentes com FLUSHA + FINISH, sem esperar VSync. FINISH anteriores são consumidos
antes de iniciar uma nova espera; tickets DMA sozinhos não provam conclusão GS.
O recurso libera VRAM antes dos pixels. Essa política pode bloquear o EE nas
trocas/recriações e precisa de medições antes de introduzir liberações diferidas.

O renderer força TEX0/TEX1/CLAMP para seu contexto e restaura o estado salvo
depois de uma barreira, junto com TEST/ZBUF. A escrita forçada é necessária
porque TileMap também pode escrever registros de textura fora do cache comum.

## VU1, recorte e validação

O novo microprograma preserva os programas aceitos de cor/difuso. UV ocupa
UNPACK V2-32 em 146..193; saída começa em 194, no buffer VU1 existente.
Ele emite S=u/W, T=v/W e Q=1/W. GS MODULATE usa RGB 128 como valor neutro.
Objetos contidos podem usar iluminação VU1; interseções recebem iluminação
original e UV interpoladas no recorte homogêneo C, antes da divisão por W.

`geometryBytes` inclui 8 bytes adicionais por vértice/padding texturizado:
24 unlit contido, 36 diffuse contido e 28 recortado. Uploads de textura,
tags, programas, constantes, estado GS e HUD não entram nesse contador.

Os testes host cobrem cópia de pixels/UVs, validação, importação UV, retenção
Mesh/Instance/Batch, interpolações com W diferente, UNPACK e restauração GS.
Um teste compila o backend real e o serializer FINISH contra hardware simulado,
verificando upload alinhado/padding, espera anterior, release e reset de VRAM.
A suíte JS de 32 bits exercita subarrays, getters, descarte e runtimes novos
com ASan/UBSan. Builds EE e conferência OpenVCL completam os checks automáticos.
Esses testes não executam rasterização; PCSX2 e PS2 real são validações separadas.
O [registro dos checks e hashes dos artefatos](validation/3d-textures-2026-10-04.json)
registra o aceite visual C/JS no PCSX2, separado dos testes automáticos.
A validação em PS2 real permanece pendente.
