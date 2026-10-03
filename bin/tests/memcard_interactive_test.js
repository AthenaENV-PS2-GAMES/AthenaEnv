// MemoryCard guided hardware test.
//
// Walks through what only a person with the console can do: taking the card
// out and putting it back, a file left open across a removal, a card in slot
// 2 and a card pulled in the middle of an atomic save. Instructions are drawn
// on screen, results are printed to the console and summarized at the end.
//
// USE A CARD WITHOUT IMPORTANT SAVES: pulling a card during a write is part
// of the test. Everything is written below mc0:/ATHTEST (and mc1:/ATHTEST).
//
// Each step has a timeout; hold SELECT + START on any controller for one
// second to skip a step (e.g. no second card). On PCSX2, cards are removed
// and inserted in Settings > Memory Cards (eject / insert the slot).
// Run memcard_test.js first for the non-interactive API checks.

const STEP_SECONDS = 30;
const FPS = 60;
/* getInfo() talks to the card: every few frames is enough to see a change. */
const POLL_FRAMES = 6;
const ROOT = "mc0:/ATHTEST";

const font = new Font();
const BACKGROUND = Color.new(16, 24, 40);
const BAR = Color.new(80, 200, 120);
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

/* Card status without throwing: null when the service fails. */
function cardInfo(port) {
    try {
        return MemoryCard.getInfo(port);
    } catch (error) {
        return null;
    }
}

function describeCard(info) {
    if (!info) return "servico indisponivel";
    if (!info.connected) return "vazio";
    return info.type + (info.formatted ? ", formatado, " + Math.floor(info.freeBytes / 1024) + " KiB livres" :
        ", NAO formatado");
}

function patternBuffer(size, seed) {
    const data = new Uint8Array(size);
    for (let i = 0; i < size; i++) data[i] = (i * 31 + seed) & 0xFF;
    return data.buffer;
}

function sameBytes(a, b) {
    const x = new Uint8Array(a), y = new Uint8Array(b);
    if (x.length !== y.length) return false;
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
    return true;
}

// Runs one step. `frame(i)` is called every frame and returns undefined to
// keep going, or { ok, detail } / { skip } to finish. `extra()` may draw
// under the instructions. The status of both slots is shown at the bottom.
function runStep(name, lines, frame, extra) {
    const frames = STEP_SECONDS * FPS;
    let skipHeld = 0;
    let slot1 = cardInfo(0), slot2 = cardInfo(1);

    for (let i = 0; i < frames; i++) {
        Gamepad.update();
        let outcome;
        if (Gamepad.connectedPlayers().some(p => p.pressed(Gamepad.SELECT | Gamepad.START))) {
            if (++skipHeld >= FPS) outcome = { skip: "skipped by the user" };
        } else {
            skipHeld = 0;
        }
        if (!outcome) outcome = frame(i);
        if (i % (POLL_FRAMES * 5) === 0) {
            slot1 = cardInfo(0);
            slot2 = cardInfo(1);
        }

        Screen.clear(BACKGROUND);
        font.print(20, 16, "Memory Card test - " + name);
        lines.forEach((text, n) => font.print(20, 50 + n * 20, text));
        font.print(20, 350, "Slot 1: " + describeCard(slot1));
        font.print(20, 370, "Slot 2: " + describeCard(slot2));
        font.print(20, 400, "Tempo: " + Math.ceil((frames - i) / FPS) + "s   SELECT+START (1s) pula");
        if (extra) extra();
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

/* Waits until the slot is empty (`present` false) or holds a card. */
function waitCard(name, lines, port, present, check) {
    let last = null;
    return runStep(name, lines, function(i) {
        if (i % POLL_FRAMES !== 0) return undefined;
        const info = cardInfo(port);
        if (!info) return { ok: false, detail: "getInfo failed" };
        if (info.changed) last = info;
        if (info.connected !== present) return undefined;
        return check ? check(info, last) : { ok: true, detail: describeCard(info) };
    });
}

/* A formatted PS2 card in slot 1 with room for the tests. */
function slot1Ready() {
    const info = cardInfo(0);
    return info && info.type === "ps2" && info.formatted && info.freeBytes >= 1200 * 1024;
}

// --- 1. A card to work with ----------------------------------------------

const ready = runStep("preparacao", [
    "Coloque no slot 1 um cartao PS2 formatado com 1200 KiB livres.",
    "USE UM CARTAO SEM SAVES IMPORTANTES:",
    "o teste retira o cartao durante uma gravacao.",
], function(i) {
    if (i % POLL_FRAMES !== 0 || !slot1Ready()) return undefined;
    try {
        if (MemoryCard.exists(ROOT)) MemoryCard.remove(ROOT, { recursive: true });
        MemoryCard.writeFile(ROOT + "/keep.bin", patternBuffer(4096, 1));
        return { ok: true, detail: describeCard(cardInfo(0)) };
    } catch (error) {
        return { ok: false, detail: String(error) + " (" + error.code + ")" };
    }
});

if (ready) {
    // --- 2. Removal and insertion -----------------------------------------

    waitCard("retirar cartao", ["Retire o cartao do slot 1."], 0, false, function() {
        try {
            MemoryCard.list("mc0:/");
            return { ok: false, detail: "list() worked without a card" };
        } catch (error) {
            if (error.code !== "NO_CARD") return { ok: false, detail: "code " + error.code };
            return { ok: true, detail: "operations fail with NO_CARD" };
        }
    });

    waitCard("inserir cartao", ["Coloque o mesmo cartao de volta no slot 1."], 0, true, function(info, change) {
        if (!change) return { ok: false, detail: "getInfo().changed was never true" };
        try {
            const data = MemoryCard.readFile(ROOT + "/keep.bin");
            if (!sameBytes(data, patternBuffer(4096, 1))) return { ok: false, detail: "keep.bin differs" };
        } catch (error) {
            return { ok: false, detail: "keep.bin: " + error + " (" + error.code + ")" };
        }
        return { ok: true, detail: "changed reported, keep.bin readable" };
    });

    // --- 3. A file open across a removal ----------------------------------

    let file = null;
    try {
        file = MemoryCard.open(ROOT + "/keep.bin", "r");
        file.read(100);
    } catch (error) {
        record("FAIL", "arquivo aberto", "open: " + error + " (" + error.code + ")");
    }
    if (file) {
        const out = waitCard("arquivo aberto: retirar", [
            "Um arquivo esta aberto no cartao.",
            "Retire o cartao do slot 1.",
        ], 0, false);
        const back = out && waitCard("arquivo aberto: inserir", [
            "Coloque o cartao de volta no slot 1.",
        ], 0, true);
        if (back) {
            try {
                file.read(100);
                record("FAIL", "arquivo aberto: handle antigo", "read() worked after the card left");
            } catch (error) {
                record(error.code === "CARD_CHANGED" ? "PASS" : "FAIL", "arquivo aberto: handle antigo",
                    "read() threw " + error.code);
            }
            try {
                file.close();
                /* The driver handle must be back: three opens at once. */
                const files = [0, 1, 2].map(n => MemoryCard.open(ROOT + "/h" + n, "w"));
                files.forEach(f => f.close());
                record("PASS", "arquivo aberto: handles livres", "three files opened after the removal");
            } catch (error) {
                record("FAIL", "arquivo aberto: handles livres", String(error) + " (" + error.code + ")");
            }
        } else {
            try { file.close(); } catch (error) { /* already reported */ }
        }
    }

    // --- 4. Card pulled during an atomic save ----------------------------

    const SAVE = ROOT + "/save.bin";
    const oldSave = patternBuffer(8192, 2);
    const newSave = patternBuffer(1024 * 1024, 3);
    let pulled = null;
    for (let attempt = 1; attempt <= 3 && !pulled && slot1Ready(); attempt++) {
        try {
            /* The previous version the atomic write must preserve. */
            MemoryCard.writeFile(SAVE, oldSave);
        } catch (error) {
            record("FAIL", "save atomico", "setup: " + error + " (" + error.code + ")");
            break;
        }
        let job = MemoryCard.writeFileAsync(SAVE, newSave, { atomic: true });
        let status = MemoryCard.poll(job);
        runStep("save atomico (tentativa " + attempt + ")", [
            "Um save de 1 MiB esta sendo gravado (atomic: true).",
            "RETIRE O CARTAO DO SLOT 1 enquanto a barra avanca.",
        ], function() {
            status = MemoryCard.poll(job);
            if (status.state === "running") return undefined;
            if (status.state === "done") return { skip: "the save finished before the card left" };
            pulled = status;
            return { ok: true, detail: "the write failed with " + status.error.code };
        }, function() {
            drawBar(20, 120, 400, status.bytesDone / (status.bytesTotal || 1));
        });
        if (!pulled) MemoryCard.wait(job);
        job = null;
    }
    if (pulled) {
        waitCard("save atomico: inserir", ["Coloque o cartao de volta no slot 1."], 0, true, function() {
            try {
                const tempExists = MemoryCard.exists(SAVE + "~");
                if (MemoryCard.exists(SAVE)) {
                    const data = MemoryCard.readFile(SAVE);
                    if (sameBytes(data, oldSave))
                        return { ok: true, detail: "previous save intact" + (tempExists ? ", save.bin~ left over" : "") };
                    if (sameBytes(data, newSave))
                        return { ok: true, detail: "the new save was already swapped in" };
                    return { ok: false, detail: "save.bin is neither version (" + data.byteLength + " bytes)" };
                }
                /* Pulled between the delete and the rename: the new data waits in "~". */
                if (tempExists && sameBytes(MemoryCard.readFile(SAVE + "~"), newSave))
                    return { ok: true, detail: "pulled mid-swap: complete new save in save.bin~" };
                return { ok: false, detail: "no usable save left" };
            } catch (error) {
                return { ok: false, detail: String(error) + " (" + error.code + ")" };
            }
        });
    }

    // --- 5. Slot 2 ---------------------------------------------------------

    waitCard("slot 2", [
        "Coloque um cartao PS2 formatado no slot 2",
        "(ou SELECT+START se nao tiver um segundo cartao).",
    ], 1, true, function(info) {
        if (info.type !== "ps2" || !info.formatted || info.freeBytes < 16 * 1024)
            return { skip: "card in slot 2: " + describeCard(info) };
        try {
            const data = patternBuffer(10000, 4);
            MemoryCard.writeFile("mc1:/ATHTEST/slot2.bin", data);
            const ok = sameBytes(MemoryCard.readFile("mc1:/ATHTEST/slot2.bin"), data) &&
                !MemoryCard.exists(ROOT + "/slot2.bin");
            MemoryCard.remove("mc1:/ATHTEST", { recursive: true });
            return { ok: ok, detail: ok ? "round trip on mc1:" : "mc1: content differs" };
        } catch (error) {
            return { ok: false, detail: String(error) + " (" + error.code + ")" };
        }
    });

    // --- Cleanup -------------------------------------------------------------

    try {
        if (cardInfo(0) && cardInfo(0).connected && MemoryCard.exists(ROOT))
            MemoryCard.remove(ROOT, { recursive: true });
    } catch (error) {
        record("FAIL", "limpeza", String(error) + " (" + error.code + ")");
    }
}

// --- Summary ----------------------------------------------------------------

const failures = results.filter(r => r.status === "FAIL").length;
const passes = results.filter(r => r.status === "PASS").length;
const skips = results.filter(r => r.status === "SKIP").length;
const summary = passes + " passed, " + failures + " failed, " + skips + " skipped";
console.log("Result: " + summary);

for (let i = 0; i < 10 * FPS; i++) {
    Gamepad.update();
    Screen.clear(BACKGROUND);
    font.print(20, 16, "Memory Card test - resultado: " + summary);
    results.slice(-18).forEach((r, n) => font.print(20, 50 + n * 20, r.line));
    Screen.flip();
}

if (failures !== 0) throw new Error("MemoryCard interactive tests failed");
