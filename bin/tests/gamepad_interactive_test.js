// Gamepad guided hardware test.
//
// Walks through every button, both sticks, pressure, rumble, analog-lock and
// hot-plug on a DualShock 2, then the optional hardware: a multitap, a
// DualShock 3/4 over USB and, with a USB Bluetooth adapter, the same pad
// paired and used wirelessly. Instructions are drawn on screen and results
// are printed to the console and summarized at the end.
//
// Each step has a timeout; hold SELECT + START on any controller for one
// second to skip a step (e.g. hardware you do not have).
// Run gamepad_test.js first for the non-interactive API checks.

const STEP_SECONDS = 15;
const FPS = 60;

// Optional drivers start disabled; this test covers all of them. Enabling them
// before the first update keeps the start-up player order.
Gamepad.configure({ multitap: true, usb: true, bluetooth: true });

const font = new Font();
const BACKGROUND = Color.new(16, 16, 40);
const BAR = Color.new(80, 160, 255);
const BAR_BACK = Color.new(50, 50, 70);

const results = [];

function record(status, name, detail) {
    const line = "[" + status + "] " + name + (detail ? ": " + detail : "");
    console.log(line);
    results.push({ status, line });
}

function drawBar(x, y, width, value) {
    Draw.rect(x, y, width, 10, BAR_BACK);
    const filled = Math.floor(Math.max(0, Math.min(1, value)) * width);
    if (filled >= 1) Draw.rect(x, y, filled, 10, BAR);
}

// Runs one step. `frame(player)` is called every frame after Gamepad.update()
// and returns undefined to keep going, or { ok, detail } / { skip } to finish.
// `extra(player)` may draw live values under the instructions.
function runStep(name, lines, frame, extra) {
    const frames = STEP_SECONDS * FPS;
    let skipHeld = 0;

    for (let i = 0; i < frames; i++) {
        Gamepad.update();
        const player = activePlayer();

        // Any controller can skip: in some steps the one under test is unplugged.
        let outcome;
        if (Gamepad.connectedPlayers().some(p => p.pressed(Gamepad.SELECT | Gamepad.START))) {
            if (++skipHeld >= FPS) outcome = { skip: "skipped by the user" };
        } else {
            skipHeld = 0;
        }
        if (!outcome) outcome = frame(player, i);

        Screen.clear(BACKGROUND);
        font.print(20, 16, "Gamepad test - " + name);
        lines.forEach((text, n) => font.print(20, 50 + n * 20, text));
        const remaining = Math.ceil((frames - i) / FPS);
        font.print(20, 400, "Tempo: " + remaining + "s   SELECT+START (1s) pula");
        if (player && player.connected) {
            font.print(20, 370, "Jogador " + player.index + " (" + describe(player) + ")  " +
                typeName(player.type) + (player.analog ? " analog" : " digital") +
                "  botoes 0x" + player.buttons.toString(16));
        }
        if (extra) extra(player);
        Screen.flip();

        if (outcome) {
            if (outcome.skip) record("SKIP", name, outcome.skip);
            else record(outcome.ok ? "PASS" : "FAIL", name, outcome.detail);
            return outcome.ok === true;
        }
    }
    record("FAIL", name, "timed out after " + STEP_SECONDS + "s");
    return false;
}

// The controller under test: the first connected port, fixed once chosen.
let chosen = null;

function activePlayer() {
    if (chosen) return chosen;
    return Gamepad.connectedPlayers()[0] || null;
}

function describe(player) {
    if (player.connection === "port")
        return "porta " + (player.port + 1) + "ABCD"[player.slot];
    return player.connection || "-";
}

function typeName(type) {
    for (const name of Object.keys(Gamepad)) {
        if (name.startsWith("TYPE_") && Gamepad[name] === type) return name.slice(5);
    }
    return String(type);
}

// Asks a yes/no question: CROSS = yes, CIRCLE = no, on any controller.
function confirm(name, lines, during, extra) {
    runStep(name, lines.concat(["", "X = sim     O = nao"]), function(player, frame) {
        if (during) during(player, frame);
        const answer = Gamepad.findJustPressed(Gamepad.CROSS) ? true :
            Gamepad.findJustPressed(Gamepad.CIRCLE) ? false : null;
        if (answer === true) return { ok: true };
        if (answer === false) return { ok: false, detail: "user reported failure" };
        return undefined;
    }, extra);
}

// --- 1. Detection ---------------------------------------------------------

const connected = runStep("deteccao", [
    "Conecte um DualShock 2 na porta 1 ou 2.",
], function(player) {
    if (!player) return undefined;
    if (player.type === Gamepad.TYPE_DUALSHOCK && player.analog && player.hasRumble)
        return { ok: true, detail: "port " + player.port };
    return undefined;
});

chosen = activePlayer();
if (!connected || !chosen) {
    console.log("No configured controller; aborting the guided test.");
} else {
    const pad = chosen;

    // --- 2. Every button, one at a time -----------------------------------

    const BUTTONS = [
        "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "UP", "DOWN", "LEFT", "RIGHT",
        "L1", "R1", "L2", "R2", "L3", "R3", "START", "SELECT",
    ];

    for (const name of BUTTONS) {
        const mask = Gamepad[name];
        let presses = 0;
        let releases = 0;
        let wrong = 0;

        runStep("botao " + name, [
            "Pressione e solte " + name + " uma vez.",
            "Nenhum outro botao deve ser detectado.",
        ], function(player) {
            if (player.justPressed(mask)) presses++;
            if (player.justReleased(mask)) releases++;
            if (player.buttons & ~mask) wrong |= player.buttons & ~mask;
            if (releases === 0) return undefined;
            if (presses !== 1)
                return { ok: false, detail: "justPressed fired " + presses + " times" };
            if (wrong && name !== "START" && name !== "SELECT")
                return { ok: false, detail: "unexpected buttons 0x" + wrong.toString(16) };
            return { ok: true };
        });
    }

    // --- 3. Combination ---------------------------------------------------

    runStep("combinacao L1+R1", [
        "Segure L1 e R1 juntos.",
        "pressed(L1 | R1) so deve valer com os dois.",
    ], function(player) {
        const both = Gamepad.L1 | Gamepad.R1;
        const one = player.pressed(Gamepad.L1) !== player.pressed(Gamepad.R1);
        if (one && player.pressed(both))
            return { ok: false, detail: "combination reported with one button" };
        if (player.justPressed(both)) return { ok: true };
        return undefined;
    });

    // --- 4. Sticks --------------------------------------------------------

    const stickTargets = [
        ["esquerdo para a DIREITA", false, s => s.x > 0.9],
        ["esquerdo para CIMA", false, s => s.y < -0.9],
        ["direito para a ESQUERDA", true, s => s.x < -0.9],
        ["direito para BAIXO", true, s => s.y > 0.9],
    ];

    for (const [label, right, reached] of stickTargets) {
        let hit = false;
        runStep("analogico " + label, [
            "Empurre o analogico " + label + " ate o fim",
            "e depois solte.",
        ], function(player) {
            const stick = right ? player.rightStick() : player.leftStick();
            if (reached(stick)) hit = true;
            if (hit && stick.x === 0 && stick.y === 0) return { ok: true };
            return undefined;
        }, function(player) {
            const stick = right ? player.rightStick() : player.leftStick();
            font.print(20, 150, "x " + stick.x.toFixed(2) + "  y " + stick.y.toFixed(2) +
                "   deadzone " + player.deadzone.toFixed(2));
            drawBar(20, 180, 300, (stick.x + 1) / 2);
            drawBar(20, 200, 300, (stick.y + 1) / 2);
        });
    }

    // --- 5. Pressure ------------------------------------------------------

    if (!pad.hasPressure) {
        record("SKIP", "pressao", "pressure mode not enabled on this controller");
    } else {
        let partial = false;
        let full = false;
        runStep("pressao", [
            "Pressione X devagar ate o fundo e solte.",
            "Deve aparecer um valor intermediario e 1.00.",
        ], function(player) {
            const value = player.pressure(Gamepad.CROSS);
            if (value > 0.1 && value < 0.9) partial = true;
            if (value === 1) full = true;
            if (partial && full && value === 0) return { ok: true };
            return undefined;
        }, function(player) {
            const value = player.pressure(Gamepad.CROSS);
            font.print(20, 150, "pressure(CROSS) = " + value.toFixed(2));
            drawBar(20, 180, 300, value);
        });
    }

    // --- 6. Rumble --------------------------------------------------------

    confirm("motor grande", [
        "O motor GRANDE deve variar de fraco a forte.",
        "Voce sentiu a variacao?",
    ], function(player, frame) {
        player.rumble(((frame * 4) % 256) / 255);
    });
    pad.stopRumble();

    confirm("motor pequeno", [
        "O motor PEQUENO deve pulsar (liga/desliga).",
        "Voce sentiu?",
    ], function(player, frame) {
        player.rumble(0, Math.floor(frame / 20) % 2);
    });
    pad.stopRumble();

    confirm("parar vibracao", ["Os motores pararam completamente?"]);

    confirm("vibracao com duracao", [
        "A cada 2s os dois motores vibram por 300 ms",
        "e param sozinhos. Confirma?",
    ], function(player, frame) {
        if (frame % 120 === 0) player.rumble(1, 1, 300);
    });
    pad.stopRumble();

    // --- 7. Analog lock ---------------------------------------------------

    const pressureCapable = pad.hasPressure;
    pad.setAnalog(true, false);
    runStep("botao ANALOG destravado", [
        "Aperte o botao ANALOG do controle.",
        "O controle deve ir para o modo digital (LED apagado).",
    ], function(player) {
        if (!player.analog) {
            if (player.type !== Gamepad.TYPE_DUALSHOCK)
                return { ok: false, detail: "type changed to " + player.type };
            return { ok: true };
        }
        return undefined;
    });

    runStep("ANALOG de volta pelo botao", [
        "Aperte ANALOG de novo (LED aceso).",
        "Pressao e vibracao devem ser reativadas.",
    ], function(player) {
        if (player.analog && player.hasRumble &&
            (player.hasPressure || !pressureCapable))
            return { ok: true };
        return undefined;
    });

    pad.setAnalog(false, true);
    runStep("setAnalog(false, lock)", [
        "Aguarde: o controle deve ir para digital sozinho.",
        "Apertar ANALOG agora nao deve ter efeito.",
    ], function(player, frame) {
        if (!player.analog && frame > 3 * FPS) return { ok: true };
        if (player.analog && frame > 3 * FPS)
            return { ok: false, detail: "controller left digital mode" };
        return undefined;
    });

    pad.setAnalog(true);
    runStep("setAnalog(true)", [
        "Aguarde: o modo analogico travado deve voltar sozinho.",
    ], function(player) {
        if (player.analog && player.hasRumble) return { ok: true };
        return undefined;
    });

    // --- 8. Hot-plug ------------------------------------------------------

    const where = "porta " + (pad.port + 1);
    let disconnectEdges = 0;
    const disconnected = runStep("desconexao", [
        "Remova o controle da " + where + ".",
    ], function() {
        if (pad.justDisconnected) disconnectEdges++;
        if (!pad.connected) {
            if (pad.buttons !== 0 || pad.hasRumble || pad.analog || pad.connection !== null)
                return { ok: false, detail: "state not cleared on disconnect" };
            if (disconnectEdges !== 1)
                return { ok: false, detail: "justDisconnected fired " + disconnectEdges + " times" };
            return { ok: true };
        }
        return undefined;
    });

    if (disconnected) {
        let connectEdges = 0;
        runStep("reconexao", [
            "Conecte o controle novamente na " + where + ".",
            "Ele deve voltar como o mesmo jogador " + pad.index + ",",
            "configurado (analogico + vibracao).",
        ], function() {
            if (pad.justConnected) connectEdges++;
            if (pad.connected && pad.analog && pad.hasRumble) {
                if (connectEdges !== 1)
                    return { ok: false, detail: "justConnected fired " + connectEdges + " times" };
                return { ok: true, detail: "player " + pad.index + " on " + describe(pad) };
            }
            return undefined;
        });
    }
}

// --- 9. Multitap ----------------------------------------------------------

if (!Gamepad.drivers().multitap.ready) {
    record("SKIP", "multitap", "mtapman is not ready");
} else {
    runStep("multitap", [
        "Conecte um multitap na porta 1 ou 2 com pelo menos",
        "dois controles (ou segure SELECT+START para pular).",
    ], function() {
        const tapped = [0, 1].filter(port => Gamepad.hasMultitap(port));
        const onSlots = Gamepad.connectedPlayers().filter(p => p.connection === "port" &&
            tapped.includes(p.port));
        if (tapped.length === 0 || onSlots.length < 2) return undefined;
        const slots = new Set(onSlots.map(p => p.port + "." + p.slot));
        if (slots.size !== onSlots.length)
            return { ok: false, detail: "two players share a slot" };
        return { ok: true, detail: onSlots.map(describe).join(", ") };
    });

    confirm("multitap: botoes por jogador", [
        "Aperte X em cada controle do multitap.",
        "O jogador que apertou aparece abaixo. Correto?",
    ], null, function() {
        const who = Gamepad.connectedPlayers().filter(p => p.pressed(Gamepad.CROSS));
        font.print(20, 150, "X: " + (who.map(p => "jogador " + p.index + " (" + describe(p) + ")").join(", ") || "-"));
    });
}

// --- 10. DualShock 3/4 over USB -------------------------------------------

function findConnection(connection) {
    return Gamepad.connectedPlayers().find(p => p.connection === connection) || null;
}

const ds34Types = [Gamepad.TYPE_DUALSHOCK3, Gamepad.TYPE_DUALSHOCK4];
let usbPad = null;

if (!Gamepad.drivers().usb.ready) {
    record("SKIP", "DualShock 3/4 USB", "ds34usb is not ready");
} else {
    runStep("DualShock 3/4 USB", [
        "Conecte um DualShock 3 ou 4 por cabo USB.",
        "No DualShock 3, aperte o botao PS.",
    ], function() {
        usbPad = findConnection("usb");
        if (!usbPad) return undefined;
        if (!ds34Types.includes(usbPad.type))
            return { ok: false, detail: "type " + usbPad.type };
        if (!usbPad.analog || !usbPad.hasRumble)
            return { ok: false, detail: "analog/rumble not reported" };
        return { ok: true, detail: "player " + usbPad.index + ", " + typeName(usbPad.type) };
    });

    if (usbPad) {
        const buttons = ["CROSS", "L2", "UP", "START"];
        for (const name of buttons) {
            runStep("USB botao " + name, [
                "No controle USB, aperte " + name + ".",
            ], function() {
                if (usbPad.justPressed(Gamepad[name])) return { ok: true };
                return undefined;
            });
        }

        let moved = false;
        runStep("USB analogico", [
            "Mova o analogico esquerdo do controle USB ate o fim e solte.",
        ], function() {
            const stick = usbPad.leftStick();
            if (Math.hypot(stick.x, stick.y) > 0.9) moved = true;
            if (moved && stick.x === 0 && stick.y === 0) return { ok: true };
            return undefined;
        });

        confirm("USB vibracao", [
            "O controle USB deve vibrar: forte e depois fraco.",
            "Sentiu os dois motores?",
        ], function(player, frame) {
            const phase = Math.floor(frame / 60) % 2;
            usbPad.rumble(phase ? 0 : 1, phase ? 1 : 0);
        });
        usbPad.stopRumble();
    }
}

// --- 11. Bluetooth --------------------------------------------------------

if (!Gamepad.drivers().bluetooth.ready) {
    record("SKIP", "Bluetooth", "ds34bt is not ready");
} else if (!usbPad || !usbPad.connected) {
    record("SKIP", "Bluetooth", "needs the DualShock 3/4 from the USB step to pair");
} else {
    let paired = false;
    runStep("pareamento Bluetooth", [
        "Conecte o adaptador Bluetooth USB e mantenha o",
        "DualShock 3/4 no cabo. O endereco sera gravado nele.",
    ], function(player, frame) {
        if (usbPad.connection !== "usb")
            return { ok: false, detail: "the USB controller was unplugged" };
        if (frame % 60 !== 30) return undefined;
        paired = usbPad.pairBluetooth();
        return paired ? { ok: true } : undefined;
    });

    if (paired) {
        runStep("Bluetooth", [
            "Desconecte o cabo USB e aperte o botao PS.",
            "O controle deve voltar sem fio.",
        ], function() {
            const wireless = findConnection("bluetooth");
            if (!wireless) return undefined;
            if (!ds34Types.includes(wireless.type))
                return { ok: false, detail: "type " + wireless.type };
            return { ok: true, detail: "player " + wireless.index };
        });

        const wireless = findConnection("bluetooth");
        if (wireless) {
            runStep("Bluetooth botao X", ["No controle sem fio, aperte X."], function() {
                if (wireless.justPressed(Gamepad.CROSS)) return { ok: true };
                return undefined;
            });
            confirm("Bluetooth vibracao", ["O controle sem fio esta vibrando?"], function() {
                wireless.rumble(0.7, 1);
            });
            wireless.stopRumble();
        }
    }
}

// --- Summary --------------------------------------------------------------

const failures = results.filter(r => r.status === "FAIL").length;
const passes = results.filter(r => r.status === "PASS").length;
const skips = results.filter(r => r.status === "SKIP").length;
const summary = passes + " passed, " + failures + " failed, " + skips + " skipped";
console.log("Result: " + summary);

for (let i = 0; i < 10 * FPS; i++) {
    Gamepad.update();
    Screen.clear(BACKGROUND);
    font.print(20, 16, "Gamepad test - resultado: " + summary);
    results.slice(-18).forEach((r, n) => font.print(20, 50 + n * 20, r.line));
    Screen.flip();
}

if (failures !== 0) throw new Error("Gamepad interactive tests failed");
