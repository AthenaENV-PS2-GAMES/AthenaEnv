# GLTF3D

Cenas glTF 2.0 / GLB com hierarquia e animações. `Model3D.load()` continua
lendo uma malha por arquivo; `GLTF3D.load()` lê a cena inteira. Ative com
`node tools/modules.js configure --modules=...,gltf3d`.

```js
const hero = GLTF3D.load("models/hero.glb", { shading: Model3D.DIFFUSE });
scene.root.add(hero.root);
const walk = new Animation3D.Player(hero.clips.walk, hero.nodes);
walk.loop = true; walk.play();          // ativo até walk.dispose()
Animation3D.attachLoop(); scene.attachLoop();
```

## O que é importado

- **Nós:** cada nó vira um `Scene3D.Node` com o TRS do arquivo; um nó dado
  por matriz é decomposto em TRS (sem cisalhamento; reflexão vai para a escala
  X). A hierarquia é mantida, e `root` é um grupo com as raízes da cena padrão
  (ou todos os nós sem pai, se o arquivo não tiver cenas).
- **Malhas:** cada primitiva triangular vira uma malha do Model3D, com as
  mesmas regras de material, textura e atributos de `Model3D.load()`. A primeira
  primitiva fica no nó; as demais viram nós filhos.
- **Animações:** cada animação vira um `Animation3D.Clip`, com translação,
  rotação e escala, interpolação `LINEAR` ou `STEP`. Os alvos são os índices
  dos nós, então `new Animation3D.Player(clip, asset.nodes)` toca o clipe nos
  nós carregados. `CUBICSPLINE` mantém os valores das keys e toca linearmente.
- **Skins:** `JOINTS_0`/`WEIGHTS_0` (4 juntas por vértice) e as matrizes de
  bind inversas viram um skin do Scene3D no nó da malha. A cada `draw()`, a
  paleta `mundo(junta) × bind inversa` é calculada em espaço de mundo: o
  transform do próprio nó com skin não o move, como no glTF; mova as juntas
  (ou um ancestral delas). Uma AABB conservadora (bounds da malha por junta
  com peso) escolhe o caminho: inteiramente dentro do frustum, a paleta vai
  para o VU1 (programa `draw_3D_skinned`, até 24 juntas por malha) e cada
  vértice é deformado lá; fora, a malha é descartada; cruzando um plano (ou
  com mais de 24 juntas, ou textura), posição e normal são deformadas em C e
  recortadas. No PS2, uma coluna de 2334 vértices custa ~585 µs de draw no VU1
  contra ~5060 µs em C ([medição](benchmarks/3d-skinning-2026-10-06.json)).
  Subárvores com malhas skinned não são descartadas pelo culling hierárquico.
- **Morph targets:** deltas `POSITION`/`NORMAL` de até 8 targets por
  primitiva (accessors esparsos aceitos; `TANGENT` ignorado), pesos iniciais do
  nó ou da malha e canais `weights` como trilhas `"weights"` do Animation3D.
  `node.setWeights([...])`/`getWeights()` controlam os pesos pelo JS. Com algum
  peso diferente de zero, a malha é misturada a cada `draw()` (base +
  Σ peso × delta). No VU1 (`vu1/draw_3D_morph.vcl`) quando possível: até 4
  targets ativos, sem deltas de normal, bounds dentro do guard band e sem
  skin; normais geradas por face são refeitas do triângulo misturado. Nos
  outros casos, em C antes do desenho (e antes do skinning), com normais
  renormalizadas. Os bounds do nó crescem pelos deltas ponderados. No PS2, uma
  caixa de 2304 vértices custa ~296 µs a mais que parada no VU1, contra
  ~1004 µs na mistura em C
  ([medição](benchmarks/3d-morph-vu1-2026-10-06.json)); pesos zerados usam o
  caminho normal, sem custo. Pesos animados chegam só à primeira primitiva de
  uma malha com várias.
- **Retorno:** `{ root, nodes, names, clips }`; os handles têm referência
  própria e podem ser descartados em qualquer ordem.

## Ainda não suportado

Mais de 8 morph targets por primitiva, extensões KHR
além de `KHR_materials_unlit`, imagens embutidas e samplers com repeat: esses
arquivos são recusados com erro, nunca importados pela metade.

`tools/make_3d_animated_asset.js` gera `bin/models/arm.glb` (hierarquia,
matriz, duas primitivas, interpolações) e `tools/make_3d_skinned_asset.js`
gera `bin/models/bend.glb` (coluna com duas juntas); `tools/make_3d_morph_asset.js`
gera `bin/models/morph.glb` (cubo com dois targets, um esparso, e o clipe
`morph`; `--grid=8 --out=morph_hi.glb` faz a versão de 2304 vértices de
`bin/morph_profile.js`). Os arquivos são usados pelos
testes e por `bin/gltf3d_example.js` (`--cfg=gltf3d_example.ini`). Com
`--rings=33 --sides=12 --out=bend_hi.glb` ela gera a coluna de 2334 vértices
de `bin/skin_profile.js` (`--cfg=skin_profile.ini`), que mede o skinning.
