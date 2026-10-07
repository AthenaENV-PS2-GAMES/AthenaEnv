# Trim de scratch Scene3D — 2026-10-07

`Scene3D.trimScratch()` libera buffers CPU globais de skinning e morph e pode
ser chamada depois de `scene.draw()` numa transição de fase. A próxima
chamada de draw com deformação CPU aloca novamente. O hook
`athena_scene3d_module_shutdown` também libera os buffers; a operação é
idempotente. O scratch não é referenciado por DMA_REF: CPU deformado é
serializado no packet writer durante draw, e o caminho DMA_REF usa meshes
retidos pelo Render3D.

Validação: `sh tests/host/run_3d.sh` passou Scene3D, Render3D com DMA_REF ligado
e desligado, e as demais suítes 3D. O teste compara posições, normais e bounds
para deformação indexada/expandida antes e depois do trim, e chama trim duas
vezes. Os arquivos EE `scene3d.c` e QuickJS `ath_scene3d.c` compilaram usando
a toolchain PS2 SDK da imagem Docker.

Scene3D é opcional e não está selecionado na configuração ativa. Assim, o
build de produção atual não inclui a API até o projeto habilitar esse módulo.
A compilação isolada valida sintaxe/tipos; o harness host cobre o comportamento,
mas esta etapa não teve validação visual PCSX2 nem PS2 físico.
