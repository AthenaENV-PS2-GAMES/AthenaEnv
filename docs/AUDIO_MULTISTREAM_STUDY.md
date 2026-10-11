# Estudo de áudio simultâneo e preparação da medição de capacidade

Data: 07/10/2026. Escopo: loop/pause em SFX ADPCM e reprodução simultânea
de arquivos WAV e Ogg Vorbis no AthenaEnv.

**Conclusão:** a implementação de múltiplos streams é viável com um mixer
PCM no EE e uma única saída audsrv. A quantidade segura de streams ainda
não foi medida. Este documento registra a inspeção; não anuncia suporte
implementado nem garante dois ou mais OGGs em tempo real no PS2.

## Evidências e alcance

Foi inspecionado o código atual do projeto e o usuário executou comandos
de leitura na imagem Docker `athenaenv-build:latest`, ID `5e73592af683`,
sem rede. O ID curto identifica a imagem local consultada; não substitui
um digest completo para reproduzir a medição.

| Componente | Evidência encontrada |
|---|---|
| PS2DEV | `/usr/local/ps2dev` |
| PS2SDK instalado | `/usr/local/ps2dev/ps2sdk` |
| GSKit | `/usr/local/ps2dev/gsKit` |
| Fontes audsrv na imagem | `/usr/local/ps2sdk/iop/sound/audsrv/src/` |
| audsrv nas fontes | `VERSION "0.93"`, `IRX_ID(MODNAME, 1, 4)` |
| Cliente EE instalado | `ee/lib/libaudsrv.a`; headers e IRX também presentes |
| Vorbis | `libvorbis.a`, `libvorbisfile.a`, `libogg.a`; pkg-config informa 1.3.7 para vorbis/vorbisfile |

Não foram encontrados metadados Git nos caminhos consultados. A versão
declarada nas fontes não comprova sozinha que o IRX foi compilado dessas
mesmas fontes. A inspeção dos símbolos do cliente EE instalado confirmou
a API esperada, sem funções adicionais de mixer ou streams independentes.
Antes dos benchmarks, registrar hashes dos binários e a identidade completa
da imagem, além da revisão do projeto e alterações locais relevantes.

Referências locais:

- [Player WAV/OGG](../src/modules/sound/native/sound_stream.c).
- [SFX ADPCM](../src/modules/sound/native/sound_sfx.c).
- [API JavaScript/TypeScript](../src/modules/sound/sound.d.ts).
- [Bindings QuickJS](../src/modules/sound/quickjs/ath_sound.c).
- [Dependências do módulo](../src/modules/sound/module.json).
- [Dockerfile](../Dockerfile) e [serviços Docker](../docker-compose.yml).

Referências externas, para comparação com a instalação:

- [audsrv PCM oficial](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/audsrv/src/audsrv.c).
- [audsrv ADPCM oficial](https://github.com/ps2dev/ps2sdk/blob/master/iop/sound/audsrv/src/adpcm.c).
- [API EE oficial](https://github.com/ps2dev/ps2sdk/blob/master/ee/rpc/audsrv/include/audsrv.h).
- [Concorrência em libvorbisfile](https://xiph.org/vorbis/doc/vorbisfile/threads.html).

Esses links acompanham o upstream e podem mudar; as conclusões sobre a
imagem se baseiam nas saídas fornecidas pelo usuário.

## Comportamento atual

| Recurso | Situação |
|---|---|
| SFX simultâneos | Até 24 canais ADPCM, sujeitos à memória e disponibilidade das vozes |
| Loop de SFX | Codificado no `.adp`; conversor aceita `--loop`/`-L` |
| Alterar `Sfx.loop` | Setter rejeita com `UNSUPPORTED` |
| Pause/resume de `.adp` | Ausente na API atual e no cliente audsrv inspecionado |
| `Sfx.stop()` | Silencia a voz e permite reutilização; não preserva posição para resume |
| WAV/OGG | Um stream ativo; `play()` em outro substitui o atual |
| Loop/pause de stream | Já disponíveis para o único stream ativo |
| Música e ADPCM juntos | Caminhos coexistentes no sistema atual |

Exemplo de loop já disponível:

```sh
node tools/wav2adp.js --loop ambiente.wav ambiente.adp
```

O loop depende das flags dos blocos ADPCM, além dos metadados do cabeçalho.
Mudar apenas uma propriedade não reconfigura a amostra na SPU2.
SFXs compartilham aproximadamente 2 MiB de RAM SPU2: o audsrv começa a
alocar em `0x5010` e verifica o limite de 2.097.152 bytes. Espaços liberados
entre amostras não são imediatamente reaproveitados pelo alocador.

Para pause ADPCM, silenciar o volume não congela a reprodução. Uma extensão
no IOP que ajuste pitch para zero e depois o restaure é uma hipótese a
investigar, não uma solução validada. Exigiria testar posição, envelope,
loop, reserva do canal e comportamento em hardware. Esse trabalho é
independente do mixer WAV/OGG.

## Limitações do driver e das dependências

As fontes audsrv da imagem mostram uma fila `ringbuf[20480]`, um par de
índices de leitura/escrita e formato e estado de reprodução globais.
`audsrv_play_audio()` alimenta essa saída compartilhada. Chamadas de várias
fontes enfileiram bytes; não somam seus sinais. Duplicar threads ou players
sem um mixer não implementa sobreposição.

PCM de 16 bits, estéreo, 48 kHz é aceito diretamente. Há também combinações
específicas de 11.025, 12.000, 22.050, 24.000, 32.000 e 44.100 Hz, com
restrições de canais/profundidade. O projeto já converte outros formatos
no EE; um mixer precisará converter todas as fontes para uma saída comum,
mesmo quando cada formato seria aceito individualmente pelo driver.

A fila máxima de 20.480 bytes equivale a cerca de 106,7 ms em 48 kHz,
estéreo, 16 bits. Isso é duração de capacidade, não latência constante.
A ocupação real, os buffers DMA e a antecipação da mixagem influenciam
a latência percebida.

libvorbisfile permite várias instâncias `OggVorbis_File`; o acesso a uma
mesma instância deve ser serializado. O projeto já tem estado de arquivo
e decodificador por objeto. Não existe um limite de um OGG imposto por
essa API, mas o port instalado precisa ser exercitado com arquivos reais.

`nm -u libvorbis.a` mostrou referências a `__adddf3`, `__divdf3`,
`__muldf3`, `__subdf3`, conversões de double e funções matemáticas como
`cos`, `pow`, `log` e `sqrt`. Isso indica operações de double por helpers
de software em partes da biblioteca. Não informa frequência de execução,
custo por arquivo ou limite de streams. Medir antes de decidir por outro
decodificador, como uma alternativa de ponto fixo, que não foi avaliada.

Não foi identificado bloqueio em GSKit ou na toolchain PS2DEV. As
dependências diretamente envolvidas são audsrv/libsd e Vorbis/Ogg.
Mixagem no EE pode preservar os binários atuais do audsrv.

## Mudanças necessárias no projeto

O player atual tem `player.current`, uma fila de blocos, geração de leitura,
estado de drain, seek e fade globais. Pause, troca de formato e destruição
podem alterar toda a fila. Não basta substituir `current` por uma lista.

Arquitetura proposta, ainda não implementada:

```text
WAV/OGG por fonte → leitura/decodificação → conversão → fila por fonte
                 → volume/pan/fade → mixer → fila curta → feeder → audsrv
```

- Um feeder deve ser o responsável pela saída PCM; evitar RPC bloqueante
  que impeça o atendimento dos SFXs.
- Formato fixo inicial proposto: 48 kHz, estéreo, 16 bits. A conversão deve
  manter fase por fonte. O conversor linear existente é reaproveitável,
  mas redução de taxa exige avaliar aliasing e qualidade.
- Soma com acumulador de pelo menos 32 bits, ganho e saturação explícitos.
  Para cada limite configurado, verificar overflow intermediário.
- Filas, estados de reprodução, fades, loop, seek e geração de descarte
  independentes por fonte. Volume global pode continuar como master.
- Rastrear posição audível de cada fonte na linha do tempo da saída.
  Eventos de loop/fim devem ocorrer ao serem ouvidos, não ao decodificar.
- Definir segurança de lifetime: uma fonte não pode ser destruída enquanto
  decodificação ou referências de segmentos ainda a utilizam.
- Reabrir arquivos e recuperar cada fonte após reset do IOP, sem usar
  descritores antigos ou reconfigurar a saída separadamente por fonte.
- Avaliar também vídeo sincronizado por `Stream.position`: esse contrato
  precisa continuar válido com múltiplas fontes.

Depois que o PCM misturado é enviado ao audsrv, não é possível remover
apenas uma fonte dele. Pause/stop/volume individuais precisam aceitar a
latência pendente ou de uma estratégia de reconstrução da saída, que pode
afetar as demais fontes. Recomenda-se antecipar decodificação sem antecipar
excessivamente a mixagem.

Um leitor único com escalonamento por prazo é uma opção inicial, mas uma
leitura bloqueada pode atrasar todas as fontes. Mais threads não fornecem
paralelismo de CPU em vários núcleos no EE e podem aumentar stacks e
contenção de I/O/RPC. A estratégia precisa ser comparada por medição.
Em falta de dados, uma política candidata é inserir silêncio apenas na
fonte atrasada; decidir se sua posição congela ou avança para manter
sincronismo. A política deve ser documentada e testada.

## Dimensionamento inicial

Hoje há 32 blocos de 4.096 bytes: 128 KiB de payload global, além de
metadados. Cada uma das duas threads é criada com stack de 32 KiB.
O conversor usa um buffer de origem de 2.048 bytes quando necessário.
Esses valores não incluem memória interna Vorbis, arquivos ou estruturas.

Estimativa para filas por fonte com 500 ms de PCM 48 kHz estéreo de 16 bits:

| Fontes | Payload total estimado |
|---|---|
| 1 | 96.000 bytes, aproximadamente 93,75 KiB |
| 2 | 192.000 bytes, aproximadamente 187,5 KiB |
| 4 | 384.000 bytes, aproximadamente 375 KiB |
| 8 | 768.000 bytes, aproximadamente 750 KiB |

Esses números não são capacidade comprovada. O áudio de saída continua
em 192.000 bytes/s independentemente do número de fontes; leitura,
decodificação, conversão e mistura crescem com as fontes. WAVs de taxas
altas podem consumir mais banda de origem; OGG reduz banda de arquivo,
mas adiciona custo de decodificação.

## Preparação do estudo de capacidade

### Etapas e entregas

1. **Baseline atual:** medir uma fonte real WAV e uma OGG, com e sem carga
   gráfica. Identificar o custo existente antes de alterar a arquitetura.
2. **Protótipo mínimo:** duas fontes, mixer fixo, filas independentes e
   métricas. Validar soma e isolamento de controles antes de medir limites.
3. **Escala:** aumentar fontes uma a uma até falha ou margem insuficiente.
   Medir WAV, OGG e combinação; não extrapolar um resultado para outro.
4. **Integração:** testar SFX ADPCM, vídeo sincronizado, reset IOP, criação,
   destruição e controles durante reprodução.
5. **Decisão:** publicar limite recomendado por perfil, formato, mídia e
   carga de jogo, com margem e política ao atingir o limite.

### Instrumentação proposta

Usar contadores e timestamps com custo limitado, sem imprimir por bloco.
Separar tempo de CPU de tempo bloqueado em I/O; medir uma chamada de decode
com leitura incluída não isola custo do codec.

| Métrica | Registro esperado |
|---|---|
| Decodificação/conversão | Tempo por fonte e por segundo de áudio produzido; distribuição e máximo |
| I/O | Tempo e bytes por leitura, atrasos e falhas, por fonte |
| Mixer/feeder | Tempo por bloco, atraso no atendimento, RPCs e bytes enviados |
| Buffers | Mínimo/máximo de ocupação por fonte e saída, faltas e duração do silêncio inserido |
| Memória | Uso inicial, pico e evolução após ciclos de abrir/fechar/reset |
| Jogo | Tempos de frame, p50/p95/p99/máximo e comparação com baseline |
| Controles | Latência audível de play/pause/stop/seek/volume, drift e eventos |
| Qualidade | Clipping, overflow, estalos, repetições indevidas e descontinuidades de loop |

Registrar resolução do relógio, unidades e overhead da instrumentação.
O prazo de processamento é a duração do bloco de saída: `frames / 48000`.
Por exemplo, 512 frames dão aproximadamente 10,67 ms; isso não obriga
o mixer a usar exatamente o bloco do driver.

### Matriz mínima

| Eixo | Casos |
|---|---|
| Fontes | 1 como baseline; 2 como primeira sobreposição; aumentar uma a uma |
| Codec | WAV+WAV, WAV+OGG, OGG+OGG; misturas em contagens maiores |
| Conteúdo | Tons determinísticos para validar soma; músicas distintas para custo real |
| Formato | 48 kHz estéreo/16-bit; 44,1 kHz; mono; mistura de taxas; WAV 8/24/32-bit e float |
| OGG | Fixar encoder, parâmetros/bitrate, duração e hashes; variar complexidade do conteúdo |
| Mídia | Cada backend de armazenamento relevante e disponível, com identificação completa |
| Carga | Áudio isolado; cena representativa; carga alta; SFXs ADPCM simultâneos |
| Controle | Início conjunto/escalonado, pause individual, seek, fades, loops de tamanhos distintos |
| Robustez | Leitura atrasada, EOF, arquivo ausente/corrompido, destruição, reset IOP |

Não usar apenas cópias do mesmo arquivo: cache e padrão de acesso podem
favorecer resultados que não representam streams distintos.

### Protocolo e critérios

Proposta inicial: 30 s de aquecimento, 180 s de medição e três repetições
por caso; depois um teste prolongado de pelo menos 30 min nos perfis
candidatos. Registrar carregamento inicial separadamente. Fixar buffers,
prioridades e configuração durante cada comparação A/B.

Para recomendar uma capacidade, exigir nos casos representativos:

- Nenhuma falta de áudio ou repetição de buffer antigo durante operação
  normal; falhas induzidas devem seguir a política documentada.
- Controles independentes, posição/eventos corretos e ausência de clipping
  não previsto pelo ganho escolhido.
- Memória estável e ausência de uso após liberação/deadlock nos ciclos.
- Margem explícita no orçamento de áudio e no tempo de frame. Definir
  antes da campanha o FPS-alvo, o orçamento de CPU e a latência aceita;
  esses requisitos ainda estão em aberto.

Se uma configuração falhar, identificar primeiro o gargalo (codec, I/O,
mixer, escalonamento ou memória). Uma contagem isolada sem esse diagnóstico
não deve virar limite global da API.

PCSX2 serve para funcionalidade e regressões. Seu desempenho não certifica
capacidade em PS2 real. Registrar modelo do console, mídia/backend, modo
de vídeo, build, otimização, instrumentação e configuração de buffers.

### Registro dos resultados

Guardar relatórios e dados em `docs/benchmarks/`, com data e revisão.
Cada caso deve registrar pelo menos:

```text
case_id, project_revision, local_changes, image_id, dependency_hashes
hardware, storage_backend, video_mode, build_flags, workload
sources: codec, rate, channels, encoding, duration, hash, encoder_settings
buffer_settings, output_format, thread_priorities, warmup_s, duration_s
decode_cpu, io_wait, mixer_time, feeder_lateness, buffer_occupancy
underruns, inserted_silence_ms, memory_peak, frame_times, control_latency
repetition, result, failure_reason
```

Separar tabela de resultados de recomendações. A capacidade será definida
por perfil e pelas medições repetidas, não apenas pelo melhor caso.

## Validação já feita e pendências

Foram executados os testes existentes de host com UBSan: SFX, 47 checks
sem falhas; streams, 99 checks sem falhas. Eles validam lógica existente
com stubs. O [stub Vorbis](../tests/host/stubs/vorbis/vorbisfile.h) rejeita
toda abertura de OGG: esses resultados não validam decodificação OGG real,
mixagem simultânea nem desempenho no EE.

Antes de medir capacidade, faltam testes de mixer com resultado PCM
conhecido, saturação/overflow, taxas diferentes e isolamento dos controles;
testes com libvorbisfile real; e o protótipo instrumentado no PS2.
O suporte multi-stream e a investigação de pause ADPCM permanecem sem
implementação neste estudo.
