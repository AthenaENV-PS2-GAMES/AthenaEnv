# Regressões de listas 2D e fonte atlas — 2026-10-07

A cobertura pendente do roadmap foi implementada sobre os renderers reais,
com 1458 casos de listas e 810 de fonte atlas. Nenhuma mudança de comportamento
nos renderers foi necessária nesta etapa.

## Cobertura

- Nove writers: pontos; linhas simples/Gouraud; triângulos simples/Gouraud;
  triângulos texturizados simples/Gouraud; sprites; retângulos texturizados.
- Rings 2048/4096/8192, capacidade -1/exata/+1, múltiplos lotes e espaço já ocupado.
- Ordem, quantidade e posição dos vértices, UVs e cores, reserva igual ao escrito,
  payload VIF DIRECT/GIF completo, canários externos, marcadores de upload.
- Câmeras identity/axis/rotated; texturas pendentes, residentes e transição
  upload/residente; falhas de bind; listas vazias, nulas e de tamanho negativo.
- Fonte atlas: 1/2/5 passes (normal/sombra/outline), 1/2 atlases, até 2300 glyphs,
  ordem por pass, culling de texto fora da câmera e rotação por retângulos reais.

`draw_packet_harness.c` usa o allocator DMA de produção, verifica cada reserva
e interpreta VIF DIRECT e os formatos GIF PACKED/REGLIST. Os testes usam
`owl_draw.c` e o renderer de `fntsys.c`; somente packing EE, DMA/GS e bind são
stubs. Fonte atlas injeta layouts antes de desenhar, portanto esse teste não
avalia rasterização FreeType, kerning ou layout de strings.

As matrizes dos fixtures usam coeficientes exatos em float32. Na preparação,
coeficientes decimais produziram diferenças de um LSB nas expectativas em
x87/i386; os fixtures finais mantêm comparação exata sem tolerância. Isso
corrigiu o teste, sem alteração no código de produção.

## Execuções e evidências

Todas as execuções automatizadas terminaram com exit code 0.

| Validação | Resultado |
| --- | --- |
| Segurança x86_64 e i386 | ASan/UBSan; 1458 casos de listas + 810 de atlas em cada ambiente |
| Host completo | Aprovado, com skips de ferramentas opcionais documentados abaixo |
| Bindings JavaScript i386 | 1424 checks aprovados, 0 falhas; 5 skips explícitos de stubs |
| Docker EE | Configuração, debug e release aprovados |
| PCSX2 Flatpak | Debug e release: PASS após 120 frames; capturas inspecionadas |

- [Segurança x86_64, ASan/UBSan/LeakSanitizer](safety.log).
- [Suíte host completa](host.log).
- [Suíte Docker i386, ASan/UBSan](javascript.log).
- [Build da imagem i386](js-image-build.log).
- [Toolchain EE: configuração, DEBUG=1 e release](ee-builds.log).
- [PCSX2 debug](pcsx2-debug.png) e [release](pcsx2-release.png).
- [Hashes, marcadores e resumos](results.json).

A imagem `athenaenv-js-tests:regressions` inclui FreeType e pkg-config; a
suíte JS executa os testes de segurança antes dos decoders e bindings. Fora
da imagem, atlas font faz skip explícito se esses headers não estiverem
instalados; a cobertura de listas continua executando.

O testador de build verificou timestamps: flags idênticas preservam objetos;
`-O1` e restauração provocam recompilação. Os builds Docker geraram
`bin/athena_regression.elf` (DEBUG=1) e `bin/athena_regression_release.elf`,
sem substituir os ELFs das etapas anteriores. O compilador emite warnings
legados de casts/formatação; os builds concluíram sem erro.

A cena PCSX2 executa Image.drawList com 1920 sprites e fonte atlas com
1344 glyphs (12 linhas) e outline, em câmera rotacionada. Ela registra PASS
após 120 frames. As capturas debug e release foram inspecionadas: grade colorida contínua,
12 linhas inclinadas e labels completos. A cena é validação funcional; o
contador de FPS da janela não é benchmark comparativo.

## Limites

Host/i386 não executam instruções de packing R5900, VU ou rasterização GS.
PCSX2 verifica os caminhos EE/GS de integração, mas não foi testado PS2 físico.
A suíte host registra skips de OpenVCL/masp e comparação adpenc, ausentes no
host, e confere 13 VSM sem hazards e 12 checks wav2adp. Skips próprios dos stubs
nos bindings JS devem ser lidos nos resumos de cada script.
A escala interna JPEG no EE continua pendente no roadmap.

## Reproduzir

```sh
env ATHENA_SAFETY_SANITIZERS=address,undefined sh tests/host/run_safety.sh
sh tests/host/run.sh
docker build -f Dockerfile.jstests -t athenaenv-js-tests:regressions .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  athenaenv-js-tests:regressions sh tests/js/run.sh
flatpak run --nosocket=wayland --socket=x11 --env=QT_QPA_PLATFORM=xcb \
  net.pcsx2.PCSX2 -batch -nofullscreen -datapath "$PWD/.validation/pcsx2" \
  -elf "$PWD/bin/athena_regression_release.elf" \
  -gameargs '--ignorecfg --script=tests/draw_lists_regression_visual.js'
```
