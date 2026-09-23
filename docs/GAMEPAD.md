# Gamepad

`Gamepad` substitui o antigo modulo `Pads`. E um singleton que atende ate oito
jogadores com qualquer combinacao de:

| Controle | Conexao | Limite | Precisa de |
|---|---|---|---|
| DualShock 2 e demais controles PS2 | portas 1 e 2 | 2 | nada |
| Idem, via multitap | slots A-D de cada porta | 8 | `configure({ multitap: true })` |
| DualShock 3 / DualShock 4 | cabo USB | 2 | `configure({ usb: true })` |
| DualShock 3 / DualShock 4 | Bluetooth (adaptador USB) | 2 | `configure({ bluetooth: true })` |

```js
const p1 = Gamepad.player(0);          // guarde uma vez

while (true) {
    Gamepad.update();                  // exatamente uma vez por frame
    if (p1.justDisconnected) pause();
    if (p1.justPressed(Gamepad.CROSS)) jump();
    const { x, y } = p1.leftStick();   // [-1, 1] com dead zone radial
    if (hit) p1.rumble(0.8, 0, 200);   // motor grande a 80% por 200 ms
    Screen.flip();
}
```

## Jogadores logicos

Com multitaps, USB e Bluetooth, "jogador = porta" deixa de fazer sentido. Os
jogadores sao logicos, como no SDL e nos consoles atuais:

- um controle que conecta ocupa o **menor jogador livre** e o mantem ate
  desconectar; reconectado, volta ao menor livre (normalmente o mesmo);
- os controles ja ligados no inicio sao atribuidos cerca de 0,5 s apos o
  primeiro `update()`, na ordem: porta 1 A-D, porta 2 A-D, USB, Bluetooth.
  Sem multitap, porta 1 e porta 2 viram os jogadores 0 e 1;
- `player.connection` (`"port"`, `"usb"`, `"bluetooth"` ou `null`),
  `player.port` e `player.slot` dizem de onde vem o controle;
- preferencias pertencem ao jogador e valem para qualquer controle atribuido
  a ele: `deadzone` e `setAnalog()`;
- para escolher quem e o jogador 0, troque os controles com
  `Gamepad.swapPlayers()`. Estado de botoes, bordas e vibracao acompanham o
  controle, sem gerar `justConnected`/`justDisconnected`:

```js
const who = Gamepad.findJustPressed(Gamepad.START);
if (who) Gamepad.swapPlayers(0, who.index);   // quem apertou START vira o jogador 0
```

## Modelo

- `Gamepad.update()` le todos os controles e congela um snapshot do frame.
  Leituras do `Player` usam esse snapshot, sem nova chamada ao hardware.
- Valores analogicos sao normalizados: sticks em `[-1, 1]`, pressao e forca de
  vibracao em `[0, 1]`.
- `type` identifica o dispositivo (`TYPE_DUALSHOCK`, `TYPE_DUALSHOCK3`,
  `TYPE_DUALSHOCK4`, ...). Um DualShock 2 continua `TYPE_DUALSHOCK` em modo
  digital; `analog` diz se os sticks estao ativos.
- A configuracao dos controles PS2 (modo analogico, pressao e motores) e uma
  maquina de estados que avanca um passo por `update()`. Nenhuma chamada
  bloqueia o frame.

## Drivers

`padman` e sempre usado: sem configurar nada, as duas portas de controle
funcionam. Os drivers opcionais sao embutidos pelo proprio modulo
(`embed_irx` no `module.json`), mas **todos comecam desligados**, porque cada
um ocupa RAM do IOP a partir do momento em que e carregado:

| Driver | Opcao | Custo aproximado na RAM do IOP |
|---|---|---|
| `mtapman` | `multitap` | 8 KB |
| `ds34usb` | `usb` | 10 KB |
| `ds34bt` | `bluetooth` | 16 KB |

O programa liga so o que usa, de preferencia antes do primeiro `update()`:

```js
Gamepad.configure({ multitap: true, usb: true });
Gamepad.update();
console.log(JSON.stringify(Gamepad.drivers()));
// {"multitap":{"enabled":true,"ready":true},"usb":{"enabled":true,"ready":true},
//  "bluetooth":{"enabled":false,"ready":false,"adapter":false}}
```

Um driver ligado e carregado no `update()` seguinte. Ligado antes do primeiro
`update()`, seus controles entram na ordem de atribuicao do inicio; ligado
depois, eles recebem jogadores conforme aparecem. Um driver que falha nao
interrompe o modulo: aparece com `ready: false` em `Gamepad.drivers()`.
Desligar um driver ja carregado libera seus controles, mas nao o descarrega do
IOP.

Os slots B-D de um multitap sao abertos na primeira vez que ele e detectado e
nao sao mais fechados: o `padPortClose()` da libpad espera a thread do padman
(sincronizada ao vblank) e custaria um ou dois frames por slot. Sem multitap,
ou com o driver desligado, esses slots apenas deixam de ser lidos.

Depois de `IOP.reset()`, o proximo `update()` recarrega tudo. O modulo
registra um hook de encerramento no `padman`, chamado antes do reset.

### Mudancas nos drivers `ds34usb` e `ds34bt`

Os fontes estao em `iop_modules/` e foram adaptados:

- **`ds34usb` nao bloqueia mais o EE.** Antes, cada `GET_DATA` fazia uma
  transferencia USB sincrona (varios ms por controle, por frame). Agora cada
  pad mantem uma transferencia de interrupcao sempre pendente, com buffer
  proprio; o callback so marca o report como pronto e acorda a thread RPC,
  que o interpreta e dispara a proxima. `GET_DATA` devolve o ultimo report e
  `SET_RUMBLE` apenas registra o pedido, aplicado pela mesma thread. A thread
  dorme ate chegar uma requisicao, um report ou um controle novo.
- **Leitura pausa quando o EE para de pedir.** Depois de 128 reports sem
  `GET_DATA` (jogo pausado, tela de loading, driver desligado no EE), o
  driver para de disparar transferencias. O proximo `GET_DATA` devolve o
  ultimo report e retoma a leitura; o frame seguinte ja tem dados novos.
- **Status com tipo:** os dois drivers marcam `0x10` no status quando o
  controle e um DualShock 4, e `GET_DATA` devolve o byte de status apos os 18
  bytes de dados. A `libds34*` antiga continua compativel (le so 18 bytes).

O modulo nao usa `libmtap`, `libds34usb` nem `libds34bt`: tem clientes RPC
proprios (`native/gamepad_iop.c`) que fazem bind com timeout em vez de laco
infinito e podem refazer o bind depois de um reset do IOP (a `libmtap` guarda
um estado estatico que nunca e limpo).

## Bluetooth

1. Ligue os dois drivers: `Gamepad.configure({ usb: true, bluetooth: true })`.
2. Conecte o adaptador Bluetooth USB e o DualShock 3/4 por cabo.
3. Confira `Gamepad.drivers().bluetooth.adapter`.
4. Com o controle atribuido a um jogador (`connection === "usb"`), chame
   `player.pairBluetooth({ overwrite: true })`. Retorna `false` quando nao
   ha adaptador.
5. Desconecte o cabo e aperte o botao PS: o controle volta com
   `connection === "bluetooth"`.

O pareamento fica gravado no controle; so precisa ser refeito ao trocar de
adaptador. **Ele substitui o pareamento anterior**: um DualShock 3 pareado
com um PS3 deixa de conectar nele. Por isso a chamada exige
`{ overwrite: true }` e o jogo deve pedir confirmacao ao usuario antes.

## Mapeamento do DualShock 3/4

| DualShock 3/4 | Gamepad |
|---|---|
| Share / Select | `SELECT` |
| Options / Start | `START` |
| Touchpad (clique, metade esquerda / direita) | `SELECT` / `START` |
| L2 / R2 | botoes e `pressure()` analogico |
| Botao PS | nao exposto (usado pelo driver para ligar e mostrar bateria) |

No DualShock 3, face, ombros e direcional tem pressao real (`hasPressure`).
No DualShock 4, apenas L2/R2 sao analogicos. O motor pequeno do DualShock 2 e
do 3 so liga/desliga; o do DualShock 4 aceita intensidade.

## API

| Membro | Descricao |
|---|---|
| `Gamepad.update()` | Poll de todos os controles; uma vez por frame |
| `Gamepad.player(index)` / `Gamepad.players` | Objeto persistente de cada jogador (0-7) |
| `Gamepad.connectedPlayers()` | Jogadores com controle, por indice |
| `Gamepad.findJustPressed(buttons)` | Primeiro jogador que acabou de apertar `buttons`, ou `null` |
| `Gamepad.configure(options)`, `Gamepad.drivers()` | Drivers opcionais; `drivers().bluetooth.adapter` indica o adaptador |
| `Gamepad.hasMultitap(port)` | Multitap presente na porta |
| `player.index`, `connected`, `justConnected`, `justDisconnected` | Identidade e bordas de conexao |
| `player.connection`, `port`, `slot` | Origem do controle |
| `player.type`, `player.analog` | Dispositivo e modo atual |
| `player.pressed/justPressed/justReleased(buttons)` | Mascaras; combinacoes exigem todos os botoes |
| `player.anyPressed/anyJustPressed(buttons)` | Basta um botao da mascara (ex.: qualquer direcional) |
| `player.repeatPressed(buttons, delayMs = 400, intervalMs = 100)` | Auto-repeat para menus, sem estado no JS |
| `player.dpad()` | Direcional como `{x, y}` em -1/0/1 |
| `player.toJSON()` | Snapshot para `console.log`/`JSON.stringify` |
| `Gamepad.swapPlayers(a, b)` | Troca os controles de dois jogadores; preferencias ficam |
| `player.buttons`, `previousButtons` | Mascaras cruas do frame atual e do anterior |
| `player.leftStick()`, `rightStick()`, `deadzone` | Sticks normalizados, dead zone radial (padrao 0.15) |
| `player.leftX`, `leftY`, `rightX`, `rightY` | Os mesmos eixos, sem alocar objeto (preferir em loops por frame) |
| `player.pressure(button)`, `hasPressure` | Pressao em `[0, 1]` |
| `player.rumble(strong, weak?, durationMs?)`, `stopRumble()`, `hasRumble` | Vibracao com parada automatica opcional |
| `player.setAnalog(enabled, lock = true)` | Modo analogico ou digital (controles PS2) |
| `player.pairBluetooth({ overwrite: true })` | Grava o endereco do adaptador no DualShock 3/4 USB |

## Diferencas em relacao ao `Pads`

| `Pads` (antigo) | `Gamepad` | Motivo |
|---|---|---|
| `Pads.get(port)` cria um objeto novo a cada chamada | `Gamepad.player(index)` retorna sempre o mesmo objeto | Evita instancias duplicadas e o wrapper `example/gamepad.js` |
| Jogador = porta; DS3/DS4 somavam botoes ao pad da mesma porta | Jogadores logicos com `connection`/`port`/`slot` | Multitap e USB/Bluetooth sem conflito |
| Sem multitap | Ate 4 controles por porta | `mtapman` embutido |
| Um DualShock 3/4 USB (argumento `1` no driver) | Dois por USB e dois por Bluetooth | Driver carregado com todos os pads habilitados |
| `pad.update()` por instancia | `Gamepad.update()` global | Um unico poll por frame |
| `waitPadReady()` com ate 1 s de espera; `ds34usb_get_data` sincrono | Configuracao assincrona; driver USB le em segundo plano | `update()` nunca trava o jogo |
| `getType()` retornava a primeira entrada da tabela de modos | `type` identifica o dispositivo, incluindo DS3/DS4; `analog` informa o modo | Tipo estavel e modo explicito |
| `setMode(port, MMODE_*, lock)` | `setAnalog(enabled, lock)` | Booleano em vez de constante crua |
| `pressed(mask)` com qualquer bit | `pressed(mask)` exige todos os bits | Combinacoes como `L1 \| R1` |
| `lx/ly/rx/ry` crus, sem dead zone | `leftStick()`/`rightStick()` normalizados | Uma unica representacao |
| `getPressure(port, btn)` 0-255, relia o hardware | `pressure(btn)` em `[0, 1]`, do snapshot | Escala unica e leitura consistente |
| `rumble(port, a, b)` com bytes crus; DS3/DS4 recebiam o mesmo valor nos dois motores | `rumble(strong, weak, durationMs)` em `[0, 1]` | Motor principal primeiro, motores independentes, parada automatica |
| `setLED` para DS3/DS4 | Removido | O driver controla o LED (indicador e bateria) e sobrescreveria o valor |
| Argumentos invalidos eram ignorados ou `SyntaxError` | `TypeError` / `RangeError` | Padrao dos modulos novos |

## Limitacoes

- Controle de LED/lightbar do DualShock 3/4 nao e exposto.
- O botao PS e o touchpad (alem do clique) nao sao lidos.
- `Pads.newEvent`/`deleteEvent` (callbacks de input) nao foram portados; use
  `justPressed`/`justReleased` no loop.

## Testes

- `bin/tests/gamepad_test.js`: contrato da API, validacao de argumentos,
  drivers, e — com o hardware presente — DualShock 2, multitap, DualShock 3/4
  por USB e Bluetooth e o tempo maximo de `update()`. Hardware ausente aparece
  como `SKIP`. A recuperacao apos `IOP.reset()` e opcional
  (`RUN_IOP_RESET_TEST`).
- `bin/tests/gamepad_interactive_test.js`: roteiro guiado na tela. Cobre o
  DualShock 2 (botoes, sticks, pressao, motores, trava ANALOG, hot-plug) e,
  opcionalmente, multitap, DualShock 3/4 por USB, pareamento e uso por
  Bluetooth. SELECT+START por 1 s em qualquer controle pula um passo.
