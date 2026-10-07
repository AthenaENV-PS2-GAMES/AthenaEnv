# Validação JPEG — 2026-10-07

Builds Docker com `athenaenv-build:latest`, UID/GID 1001:1001:
`make -j4 DEBUG=1 EE_BIN_PREF=athena_jpeg all` e
`make -j4 EE_BIN_PREF=athena_jpeg_release all` passaram.
Os ELFs gerados foram `bin/athena_jpeg.elf` e
`bin/athena_jpeg_release.elf`; hashes e marcadores estão em [summary.txt](summary.txt).
A toolchain emite warnings existentes. Os dois builds usam objetos separados
por configuração. Não foram substituídos os ELFs da primeira etapa.

PCSX2 Flatpak 2.8.2, Vulkan, perfil `.validation/pcsx2`, HostFS em `bin`:
os dois ELFs executaram `tests/jpeg_safety_visual.js` e registraram PASS após
120 frames. Inspeção das capturas confirmou quadrantes RGB na mesma orientação
do PNG/BMP, quatro níveis grayscale, JPEG 1×1 vermelho ampliado sem artefatos
visíveis e 1275 glyphs em 17 linhas. CMYK, largura 1025 e JPEG sem EOI foram
rejeitados pelo binding de Image.

- [Captura DEBUG=1](pcsx2-debug.png)
- [Captura release](pcsx2-release.png)

Para reproduzir os fixtures, execute `python3 tools/make_memory_safety_assets.py`
(Pillow necessário para JPEG). Execute:

```sh
flatpak run --nosocket=wayland --socket=x11 --env=QT_QPA_PLATFORM=xcb \
  net.pcsx2.PCSX2 -batch -nofullscreen \
  -datapath "$PWD/.validation/pcsx2" \
  -elf "$PWD/bin/athena_jpeg_release.elf" \
  -gameargs '--ignorecfg --script=tests/jpeg_safety_visual.js'
```

X11 permitiu trazer a janela à frente com wmctrl para capturas compostas com
GNOME Screenshot. As capturas finais foram inspecionadas; a captura inicial
que mostrava apenas o editor foi substituída.

A suíte host de imagens já passou com ASan/UBSan/LeakSanitizer. OOM e
`scale_down` foram cobertos no host; o binding Image usa `scale_down=false`,
portanto a cena não valida escala interna libjpeg no EE. Não houve teste em
PS2 físico nem benchmark comparativo de FPS/RAM.
