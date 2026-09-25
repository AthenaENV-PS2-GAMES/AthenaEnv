// Memory stats: shows on screen how much EE RAM and VRAM the .elf uses.
//
// "Startup" is read before anything else in this script, so it is the cost of
// the binary, the enabled modules and the QuickJS runtime alone. "Now" adds
// what this screen uses (font and text), refreshed twice per second.
// "Growth" is measured from 5 s after start, once the font cache warmed up:
// a value that stays above zero points to a leak.

const startup = System.getMemoryStats();
const ramSize = System.getCPUInfo().RAMSize;

const font = new Font();
const WHITE = Color.new(255, 255, 255);
const GRAY = Color.new(160, 160, 160);
const YELLOW = Color.new(255, 220, 0);

const kb = bytes => (bytes / 1024).toFixed(0).padStart(6) + " KB";
const mb = bytes => (bytes / 1048576).toFixed(2) + " MB";
const pct = (part, total) => (100 * part / total).toFixed(1) + "%";
const WARMUP = 5;

let lines = [];
let peakUsed = startup.used;
let baseline = null;

function growth(now, seconds) {
    if (seconds < WARMUP) return "  (after " + WARMUP + " s)";
    if (!baseline) baseline = { time: seconds, allocs: now.allocs, jsHeap: now.jsHeap };
    const minutes = (seconds - baseline.time) / 60;
    if (minutes <= 0) return "";
    const rate = bytes => ((bytes / 1024) / minutes).toFixed(1).padStart(7) + " KB/min";
    return "  native" + rate((now.allocs - now.jsHeap) - (baseline.allocs - baseline.jsHeap)) +
        "   js" + rate(now.jsHeap - baseline.jsHeap);
}

function refresh() {
    const now = System.getMemoryStats();
    const vramTotal = Screen.getMemoryStats(Screen.VRAM_SIZE);
    const vramUsed = Screen.getMemoryStats(Screen.VRAM_USED_TOTAL);
    peakUsed = Math.max(peakUsed, now.used);

    lines = [
        [YELLOW, "EE RAM                 " + mb(ramSize) + "   " + Loop.getRealElapsedTime().toFixed(0) + " s"],
        [WHITE, "  Binary (.elf)    " + kb(now.core) + "   " + pct(now.core, ramSize)],
        [WHITE, "  Native stack     " + kb(now.nativeStack)],
        [WHITE, "  Heap at startup  " + kb(startup.allocs) + "   js " + kb(startup.jsHeap)],
        [WHITE, "  Heap now         " + kb(now.allocs) + "   js " + kb(now.jsHeap)],
        [WHITE, "  JS limit         " + kb(now.jsLimit) + "   objects " + now.jsObjects],
        [WHITE, "  Used now         " + kb(now.used) + "   " + pct(now.used, ramSize)],
        [WHITE, "  Peak used        " + kb(peakUsed)],
        [WHITE, "  Free now         " + kb(System.getFreeMemory())],
        [WHITE, "Growth" + growth(now, Loop.getRealElapsedTime())],
        [YELLOW, "VRAM                   " + mb(vramTotal)],
        [WHITE, "  Used             " + kb(vramUsed) + "   " + pct(vramUsed, vramTotal)],
        [WHITE, "    static         " + kb(Screen.getMemoryStats(Screen.VRAM_USED_STATIC))],
        [WHITE, "    dynamic        " + kb(Screen.getMemoryStats(Screen.VRAM_USED_DYNAMIC))],
        [GRAY, "Heap: every malloc on the EE; js is the QuickJS share of it."],
    ];
}

// CROSS runs the cycle collector: if "js" growth drops back, it was garbage
// waiting for the GC, which only runs by itself near the JS limit.
const pad = Gamepad.player(0);
let gcResult = "CROSS: run the garbage collector";

refresh();
Loop.run(() => {
    Gamepad.update();
    if (pad.justPressed(Gamepad.CROSS)) {
        const before = System.getMemoryStats().jsHeap;
        System.gc();
        const after = System.getMemoryStats().jsHeap;
        gcResult = "GC freed " + kb(before - after).trim() + " of JS heap";
        refresh();
    }
    if (Loop.getFrameCount() % 30 === 0) refresh();
    font.color = YELLOW;
    font.print(24, 20 + 21 * lines.length, gcResult);
    let y = 20;
    for (const [color, text] of lines) {
        font.color = color;
        font.print(24, y, text);
        y += 21;
    }
}, { clearColor: Color.new(10, 10, 30) });
