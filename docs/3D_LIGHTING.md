# Primeiro incremento de materiais e iluminação

Model3D 1.1 e Render3D 1.3 acrescentam materiais copiados, normais e iluminação
Gouraud opaca. `lights` é um módulo opcional independente, incluído por
`render3d`. **As nove etapas C/JS do passe difuso foram aceitas visualmente
pelo usuário no PCSX2 em 04/10/2026.** UVs e texturas com retenção têm seu próprio
corte da fase 2.

## Executar as cenas

```sh
node tools/modules.js configure --modules=screen,loop,render3d,draw,tilemap,system,timer,usbmass
# Dentro da imagem PS2SDK no WSL/Docker:
sh tools/build_3d.sh
```

- JavaScript: `bin/athena_3d_js.elf --cfg=3d_lighting.ini`.
- C: `bin/athena_3d_lighting_native.elf`.

Execute a partir de `bin/`, preservando `models/lit_cube.glb` no mesmo diretório
relativo em ambos os runtimes. O asset de 1.436 bytes tem doze triângulos e
normais por face; pode ser reproduzido com
`node tools/make_3d_lighting_asset.js`. Dois cubos compartilham uma única malha,
com transforms independentes; o triângulo colorido exercita o recorte. A cena
dura nove etapas de 180 frames, termina sozinha e imprime o nome de cada etapa.

| Etapa | Verificar na imagem |
| --- | --- |
| 0 — unlit | Cubos com faces de cor uniforme, sem influência das luzes. |
| 1 — ambient | Cubos mais escuros; faces com a mesma intensidade ambiente. |
| 2 — directional | Faces voltadas para +Z mais claras; faces opostas recebem ambiente. |
| 3 — rotating-light | A face iluminada muda suavemente com a direção da luz. |
| 4 — four-lights | Cores diferentes por direção; desabilitar o slot 3 no frame 90 remove sua contribuição. |
| 5 — nonuniform | Cubo direito alongado, com iluminação coerente com as normais transformadas. |
| 6 — clip-sweep | Parte visível do triângulo preservada no plano próximo; cores interpoladas sem flashes ao mudar de caminho. |
| 7 — camera-tile-camera | HUD laranja correto; cubo direito azulado com outra câmera e conjunto de luzes, sem contaminar o esquerdo. |
| 8 — recreate | Sem corrupção quando malhas, instâncias e Batch são descartados após draw e recriados a cada 30 frames. |

Os quadrados no topo indicam a etapa e a barra inferior indica seu andamento.
A etapa 7 imprime somente as estatísticas do primeiro cubo nos dois runtimes;
as demais imprimem o Batch inteiro. Esses logs são diagnósticos de submissão,
não um benchmark. A execução das nove etapas foi confirmada visualmente
pelo usuário nos dois runtimes; medições de tempo deste passe ficam pendentes.

A execução desta cena foi confirmada pelo usuário nos dois runtimes: unlit
1.216 bytes, diffuse 2.128 bytes, clip-sweep 26 triângulos e 2.176 bytes no
frame recortado, e primeiro cubo de camera-tile-camera 1.008 bytes. O slot 3
foi desabilitado e as duas execuções chegaram ao término. O
[registro de aceite](validation/3d-lighting-2026-10-04.json) preserva os
contadores recebidos; ele não mede tempo de draw nem representa baseline.

## API e convenções

```js
const lights = new Lights.Set()
    .setAmbient(.15,.15,.15)
    .setDirectional(0,0,0,1,.85,.85,.85);
const mesh = Model3D.load("models/lit_cube.glb", {
    shading: Model3D.DIFFUSE,
    baseColor: new Float32Array([.75,.9,1,1])
});
const object = mesh.createInstance();
mesh.dispose();
// Dentro de draw():
Render3D.draw(object,camera,Render3D.CULL_BACK,lights);
// Ou batch.draw(camera,Render3D.CULL_BACK,lights).
```

O material é um descriptor imutável copiado para a malha. `shading` é UNLIT
por padrão; `baseColor` é RGBA linear em [0,1], branco por padrão. Sua cor
multiplica `colors` e é quantizada uma vez em RGBA8. O alpha permanece no
stream, mas o passe é opaco. Não há identidade compartilhada de Material,
edição de material nem textura neste corte.

`Geometry.normals` aceita Float32Array xyz com uma normal não nula por vértice
de entrada, incluindo malhas indexadas. O C copia, normaliza e expande o stream;
offsets de subarray são respeitados e SharedArrayBuffer é rejeitado. Sem normais
em um material DIFFUSE, o C gera normais planas por triângulo. Triângulos
degenerados sem normal explícita são rejeitados nesse modo. OBJ importa `vn`
quando todas as faces têm normais; OBJ com normais incompletas usa geração
plana. glTF/GLB importa NORMAL opcional. O loader continua limitado a uma malha
estática e não converte metallic/roughness em iluminação PBR; o passe difuso
é uma escolha explícita pelo override de material.

Cada `Lights.Set` tem ambiente e quatro slots direcionais, sem estado global.
As direções estão no mundo e **apontam para a fonte de luz**. O C normaliza
direções não nulas; RGB deve ser finito em [0,1]. O conjunto começa com ambiente
preto e slots desabilitados. `disable(slot)` e `clear()` são idempotentes;
setters inválidos não alteram estado. `revision` muda somente em atualizações
efetivas; dirty flags reconstruem a lista compactada de slots ativos somente
quando necessário. O snapshot é copiado para os pacotes por passe porque
outros módulos também usam VU1. O draw não altera o estado das luzes.

A iluminação por vértice calcula
`clamp(cor * (ambiente + soma(diffuse * max(0, dot(normal, direção)))), 0, 1)`.
Normais usam a inversa transposta da parte linear do transform, seguida de
normalização. Escalas negativas são suportadas; escalas nulas ou proporções
entre eixos superiores a aproximadamente 1.000.000 são rejeitadas para
DIFFUSE, evitando underflow na normalização do VU1. UNLIT mantém o contrato
anterior. O cálculo é linear, sem conversão sRGB, especular ou luz pontual.

Em C, os novos pontos de entrada são `athena_render3d_draw_lit` e
`athena_batch3d_draw_lit`, com `const AthenaLights *`. Os anteriores continuam
funcionando e equivalem a luzes ausentes: ambiente preto e nenhuma direcional
para DIFFUSE. Em JavaScript, as luzes são o quarto argumento de `Render3D.draw`
ou terceiro de `Batch.draw`; `undefined` permite omitir o culling. O renderer
empresta o conjunto somente durante a chamada e copia seus valores, permitindo
liberá-lo após draw mesmo antes do flush DMA.

## Renderer, memória e validação

Objetos contidos usam um novo microprograma VU1 de transformação e iluminação.
O programa de cores aceito anteriormente foi preservado. Objetos intersectando
o frustum são iluminados em C nos vértices originais; o recorte interpola essas
cores, sem recalcular luz em normais recém-geradas. Isso preserva Gouraud nas
interseções. A aritmética float do VU e a referência C em double podem diferir
em aproximadamente uma unidade de cor na quantização; imagem ainda exige
validação no alvo.

Uma normal ocupa 12 bytes mais padding no stream da malha. O payload DMA
contido DIFFUSE usa 28 bytes por vértice arredondado ao múltiplo de quatro;
UNLIT continua em 16 e a saída recortada em 20 (posição xyzw e cor). Os chunks
têm até 48 vértices. `geometryBytes` inclui normais quando transmitidas e
continua excluindo constantes/luzes, programas, tags e estado GS. Sem
alocações de scratch por frame. TEST/ZBUF são restaurados e as barreiras
preservam alternância entre programas, câmeras e TileMap.

As structs C de geometria/view foram estendidas; recompilar consumidores e
preferir inicializadores designados. Headers, bindings e declarações por
módulo são as fontes; agregados são gerados por `tools/modules.js`.

Os testes CPU com UBSan cobrem material copiado, normalização, geração de
normais, limite/atomicidade das luzes, inversa transposta, reflexão, saturação
e interpolação da cor já iluminada. Os testes de pacotes usam o renderer de
produção com stubs: verificam reservas, UNPACK de normais, snapshots de luzes,
troca de programas, restauração GS e descarte antes do flush. A suíte QuickJS
de 32 bits usa ASan/UBSan e dois runtimes novos, com classes, subarrays,
getters, GC, retenção e erros. OpenVCL/masp gera e compara o `.vsm`. Esses
testes e builds EE não executam VU1 nem substituem aceite visual no PCSX2/PS2.

UVs, texturas com retenção e sincronização de VRAM estão implementadas no
incremento seguinte, [3D_TEXTURES.md](3D_TEXTURES.md), com aceite visual pendente.
Benchmarks destes passes e validação em PS2 real permanecem no roadmap.
