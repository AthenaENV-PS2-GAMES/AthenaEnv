# Validação dos writers DMA — 2026-10-07

Correções: clear por páginas em lotes e gerenciador de programas VU
(unidades, reservas REF exatas, bounds, cache e vida útil de buffers).

| Verificação | Resultado |
| --- | --- |
| `tests/host/run_safety.sh` | PASS com ASan, UBSan e LeakSanitizer fora do sandbox. |
| `tests/host/run.sh` | PASS; inclui Render3D DMA_REF=0/1, partículas, texturas, fontes e regressões do runtime. |
| Docker DEBUG=1 / release | PASS; ELFs separados `athena_dma.elf` / `athena_dma_release.elf`. |
| PCSX2 Flatpak 2.8.2 Vulkan, cena DMA | PASS após 120 frames: 2400 sprites VU1, fonte outline, clear; inspeção da grade completa. |
| PCSX2 release, clear HD | PASS após 120 frames com superfície interna confirmada de 1920×1080 CT16, single buffer, sem Z. |

## Evidências

- [Grade VU1 e fonte](pcsx2.png).
- [Clear HD](pcsx2-hd.png).
- [Hashes e marcadores](summary.txt).
- [Saída da suíte host completa](host-tests.txt).

Os testes host de clear validam todas as páginas, inclusive as últimas,
TEST/XYOFFSET restaurados e canários com rings 2048/4096/8192. Exercitam
640×448, 1920×1080, 2048×2048, 1408×1472 (exatamente 1012 páginas) e layouts
sintéticos altos para limites exatos nos rings maiores. Esses layouts
sintéticos não representam modos de vídeo suportados.

MPG é testado com 2, 256, 258, 512 e 2048 instruções; o upload de 2048 também
usa um ring sintético de oito quadwords para forçar flushes entre tags.
A regressão inspeciona REF antes da liberação de um buffer de arquivo,
verifica unload com DMA ainda não enviado, cache com slot vazio e 17 entradas,
e rejeita alinhamento/tamanhos/unidades inválidos. A camada host não executa VU.

A cena HD usa `field: Screen.FIELD`: FRAME reduz a altura interna pela metade.
O teste exige `Screen.getMode().height === 1080` antes de emitir o marcador;
a primeira tentativa FRAME e sua captura foram substituídas. A captura FIELD
mostra fundo uniforme na região exibida e os marcadores superiores. Os
marcadores inferiores do framebuffer não aparecem no modo de apresentação
1080i atual; portanto a screenshot não comprova visualmente todas as páginas.
A ordem completa dos dois lotes é comprovada pelo teste do writer real no host;
a execução EE confirma ausência de abort na superfície de 1020 páginas.

A suíte host pulou OpenVCL/masp e comparação adpenc porque essas ferramentas
não estão instaladas no host; verificou 13 arquivos VSM sem hazards e 12
checks wav2adp. O build Docker usa os VSM existentes. O compilador ainda emite
warnings de casts DMA e código legado; os builds terminaram sem erro.
Não houve teste de cópia MMIO VU0 no PCSX2/PS2, PS2 físico, nem medição
comparativa de FPS/RAM. As variantes 2D/fonte atlas ainda precisam dos testes
diretos de fronteira indicados no roadmap.

## Reprodução

```sh
env ATHENA_SAFETY_SANITIZERS=address,undefined sh tests/host/run_safety.sh
sh tests/host/run.sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  -e ATHENAENV_SRC=/src/src athenaenv-build:latest \
  make -j4 EE_BIN_PREF=athena_dma_release all
flatpak run --nosocket=wayland --socket=x11 --env=QT_QPA_PLATFORM=xcb \
  net.pcsx2.PCSX2 -batch -nofullscreen -datapath "$PWD/.validation/pcsx2" \
  -elf "$PWD/bin/athena_dma_release.elf" \
  -gameargs '--ignorecfg --script=tests/dma_writers_visual.js'
```

Para HD, substitua o script por `tests/page_clear_hd_visual.js`.
