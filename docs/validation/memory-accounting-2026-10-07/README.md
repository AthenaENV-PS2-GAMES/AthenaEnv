# Sincronização do contador de memória — 2026-10-07

O contador de bytes úteis agora atualiza e lê em seções críticas curtas no EE.
O estado anterior de interrupções é preservado. Chamadas ao heap ficam fora
dessas seções; não foi criado outro mutex para o allocator.

## Resultados

- [Host x86_64](host.log) e [Docker i386](i386.log): ASan/UBSan/LeakSanitizer,
  oito workers × 20000 ciclos, totais exatos de blocos retidos e retorno a zero.
  Inclui realloc grow/shrink/zero/OOM, falhas de malloc/calloc/memalign,
  free(NULL) e leitura com interrupções previamente desabilitadas.
- [Suíte de segurança completa](safety.log): aprovada, incluindo DMA e fontes.
- [Builds Docker debug/release](ee-builds.log): exit code 0.
- [Build do teste EE](ee-workers-build.log): exit code 0.
- [PCSX2](pcsx2-markers.log): quatro workers nativos × 10000 ciclos com
  RotateThreadReadyQueue após malloc/realloc. Baseline e final = 420 bytes;
  sem deadlock. Os workers usam stacks estáticas e o heap real do SDK.
- Runtime release: cena de 1920 sprites e 1344 glyphs aprovada após 120 frames.
  [Captura inspecionada](pcsx2-release.png): grade e linhas completas.
- [Hashes dos três ELFs e exit codes](results.json).

## Auditoria do SDK

[Hash do checkout e disassembly do ELF ligado](sdk-locks.log) confirmam que
__malloc_lock chama __retarget_lock_acquire_recursive. O lock usa WaitSema
antes de escrever dono/contador, e só incrementa diretamente na recursão do
mesmo dono. Newlib nesta imagem habilita _RETARGETABLE_LOCKING. Não foram
alterados SDK nem libcglue; a implementação já está presente na toolchain.

Alocação/liberação continuam sendo operações de threads: o heap pode bloquear
em semáforo e não deve ser chamado em handlers de interrupção. O teste host
usa mutex pthread para simular a exclusão de CPU da seção crítica; não simula
instruções R5900. O teste EE complementa isso com libc/kernel reais no PCSX2.
Não há validação em PS2 físico ou medição de performance nesta etapa.
Snapshots durante uma chamada malloc/free em andamento podem refletir o
estado anterior à atualização contábil; os testes de totais exatos sincronizam
workers em pontos em que nenhuma operação está em andamento.

## Reprodução

```sh
ATHENA_SAFETY_SANITIZERS=address,undefined sh tests/host/run_memory.sh
# Dentro da toolchain Docker, no diretório tests/ee:
make -f Makefile.memory
# No host:
flatpak run net.pcsx2.PCSX2 -batch -nofullscreen \
  -datapath "$PWD/.validation/pcsx2" -elf "$PWD/bin/memory_accounting_ee.elf"
```

run_safety.sh inclui a nova regressão automaticamente, tanto na suíte host
quanto na suíte JS i386. O teste nativo permanece parado em SleepThread após
imprimir PASS/FAIL; o emulador foi encerrado intencionalmente após validação.
