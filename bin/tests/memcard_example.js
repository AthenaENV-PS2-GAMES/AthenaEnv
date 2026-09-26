// MemoryCard example: saving and loading a game without stopping the frame loop.
//
//   D-pad up/down  change the score
//   CROSS          save   (mc0:/ATHEXAMPLE/save.json, atomic)
//   CIRCLE         load
//   TRIANGLE       delete the save
//   START          quit
//
// The square keeps spinning while the card works: saves and loads run on a
// worker thread (the *Async calls) and are simply awaited. Take the card out
// and put it back to see the status line follow.

const SAVE_DIR = "mc0:/ATHEXAMPLE";
const SAVE = SAVE_DIR + "/save.json";
const ACCENT = Color.new(255, 180, 0);
const BAR = Color.new(80, 200, 120);

const font = new Font();
const pad = Gamepad.player(0);

const state = { score: 0, saves: 0 };
let message = "CROSS salva, CIRCLE carrega, TRIANGLE apaga";
let card = null;
let job = null;          // the job running, for the progress bar
let angle = 0;

/* Card status every half second: cheap, but it still talks to the card. */
function refreshCard() {
    try {
        card = MemoryCard.getInfo(0);
        if (card.changed && card.connected) message = "Cartao inserido no slot 1";
    } catch (error) {
        card = null;
    }
}

function describeCard() {
    if (!card) return "Memory Card: indisponivel";
    if (!card.connected) return "Memory Card: slot 1 vazio";
    if (!card.formatted) return "Memory Card: nao formatado";
    return "Memory Card: " + Math.floor(card.freeBytes / 1024) + " KiB livres";
}

/* Runs one card job at a time and reports how it ended. */
async function run(label, start) {
    if (job) return;
    try {
        job = start();
        const result = await job;
        message = label + ": ok" + (typeof result === "number" ? " (" + result + " bytes)" : "");
        return result;
    } catch (error) {
        // error.code is stable: NO_CARD, FULL, NOT_FOUND, CARD_CHANGED...
        message = label + ": " + (error.code === "NOT_FOUND" ? "nenhum save" : error.code || String(error));
    } finally {
        job = null;
        refreshCard();
    }
}

function save() {
    state.saves++;
    // atomic: a card pulled mid-save keeps the previous save.
    run("Salvar", () => MemoryCard.writeJSONAsync(SAVE, state, { atomic: true }));
    // For the PS2 browser, a save directory also needs icon.sys and the icon
    // it names (a .ico model made with an icon tool):
    //   MemoryCard.writeFile(SAVE_DIR + "/icon.sys",
    //       MemoryCard.createIconSys({ title: "Athena\nExample", icon: "icon.ico" }));
}

async function load() {
    const saved = await run("Carregar", () => MemoryCard.readJSONAsync(SAVE));
    if (saved) Object.assign(state, saved);
}

function remove() {
    run("Apagar", () => MemoryCard.removeAsync(SAVE_DIR, { recursive: true }));
}

refreshCard();

Loop.run(() => {
    Gamepad.update();
    if (pad.justPressed(Gamepad.START)) return Loop.stop();
    if (pad.justPressed(Gamepad.UP)) state.score++;
    if (pad.justPressed(Gamepad.DOWN)) state.score--;
    if (pad.justPressed(Gamepad.CROSS)) save();
    if (pad.justPressed(Gamepad.CIRCLE)) load();
    if (pad.justPressed(Gamepad.TRIANGLE)) remove();
    if (Loop.getFrameCount() % 30 === 0 && !job) refreshCard();

    // Something that must not stutter while the card works.
    angle += Loop.getDeltaTime() * 3;
    Draw.rect(300 + Math.cos(angle) * 80, 200 + Math.sin(angle) * 80, 32, 32, ACCENT);

    font.print(20, 16, "Score: " + state.score + "    saves: " + state.saves);
    font.print(20, 40, describeCard());
    font.print(20, 400, message);
    if (job) {
        const status = MemoryCard.poll(job);
        const progress = status.bytesTotal ? status.bytesDone / status.bytesTotal : 0;
        Draw.rect(20, 380, 600, 8, Color.new(50, 50, 70));
        if (progress > 0) Draw.rect(20, 380, Math.floor(600 * progress), 8, BAR);
    }
    font.print(20, 424, Loop.getStats().fps.toFixed(0) + " FPS");
}, { clearColor: Color.new(20, 20, 40) });
