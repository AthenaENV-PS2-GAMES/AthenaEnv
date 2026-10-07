# Validação da primeira etapa de segurança e RAM

Data: 2026-10-07. Código e próximos trabalhos:
[PERFORMANCE_MEMORY_ROADMAP.md](../../PERFORMANCE_MEMORY_ROADMAP.md).

| Verificação | Resultado |
| --- | --- |
| Docker: QuickJS debug, QuickJS release e runtime nativo | Passaram; ELFs distintos, sem substituir os binários anteriores. |
| Docker: `tests/host/run.sh` | Passou; inclui writer DMA e fonte bitmap de produção, runtime/módulos, 3D, áudio, vídeo e física. |
| Docker: `tests/js/run.sh` em i386 | Passou com ASan/UBSan; inclui os novos decoders com bibliotecas reais. Skips dos stubs constam nos resumos. |
| Host: decoders em x86_64 | Passaram com ASan/UBSan/LeakSanitizer, fora do sandbox de ptrace. Inclui truncamento após alocação e OOM. |
| `tools/verify_build_config.sh` na toolchain Docker | Passou: mesmas flags preservam o objeto; `-O1` e restauração causam recompilação. |
| OpenVCL/masp e detector de hazards | 13 programas conferem com o `.vsm`; 13 arquivos sem hazards detectados. |
| wav2adp, incluindo comparação PS2SDK | 14 checks, zero falhas. |
| PCSX2 2.8.2, Vulkan, debug e release | Seis imagens renderizadas; dois arquivos truncados rejeitados; 1275 glyphs em 17 linhas; marcador PASS após 120 frames. |

## Evidências

- [Resultados, hashes dos ELFs e IDs das imagens Docker](results.json).
- [Resumo dos builds, testes e marcador do PCSX2](summary.txt).
- [Captura da janela PCSX2 debug](pcsx2-debug.png).
- [Captura composta do desktop com PCSX2 release](pcsx2-release-desktop.png).

A captura release mostra a cena inteira com os assets bitmap finais. Capturas
XWD diretas da janela Vulkan mostraram atualizações parciais em algumas
ocasiões; a composição do GNOME confirmou as seis imagens e o texto completo.
A captura debug usa a primeira versão do fixture bitmap, com glyphs menores.

## Regressão encontrada no emulador

O primeiro decoder BMP passou no host, mas rejeitou a altura -64 no EE.
A assembly do cabeçalho `packed` usava LWL/LWR para carregar a altura. O teste
no PCSX2 registrou `raw-height=ffffffc0`, mas a normalização resultava em
4294967232, em vez de 64. Ao manter alinhamento nativo do DIB, preservando seu
layout de 40 bytes com `_Static_assert`, a mesma entrada passou a produzir 64.
O arquivo top-down e o bottom-up agora apresentam a mesma orientação visual.

A primeira tentativa de release também encontrou registries de objetos antigos
com outro conjunto de módulos. O stamp de configuração no Makefile eliminou
a mistura; os builds finais debug/release/native passaram sem `make clean`.

## Reproduzir a cena

Gere os fixtures com `python3 tools/make_memory_safety_assets.py` e compile
conforme o roadmap. No PCSX2, configure BIOS e HostFS e execute:

```sh
flatpak run net.pcsx2.PCSX2 -batch -nofullscreen \
  -elf "$PWD/bin/athena_safety_release.elf" \
  -gameargs '--ignorecfg --script=tests/memory_safety_visual.js'
```

A validação desta sessão usou um perfil separado em `.validation/pcsx2`, com
logs do EE habilitados, e não modificou `bin/athena.ini`.

## Limites

A compilação ainda emite warnings de código existente, como formatos de
printf e conversões de ponteiros; não houve erro de compilação nem aviso novo
nos decoders alterados. Os testes host/i386 não executam instruções VU ou o
rasterizador GS. PCSX2 cobre a execução EE/GS da cena; não houve teste em PS2
físico. A eliminação do intermediário PNG economiza uma alocação de 4 MiB no
caso RGBA 1024×1024, mas não foi feito benchmark comparativo de FPS ou medição
de toda a RAM do jogo. As demais melhorias do roadmap permanecem pendentes.
