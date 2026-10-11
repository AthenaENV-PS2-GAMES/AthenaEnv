// Memory stats: shows on screen how much EE RAM and VRAM the .elf uses.
//
// "Startup" is read before anything else in this script, so it is the cost of
// the binary, the enabled modules and the QuickJS runtime alone. "Now" adds
// what this screen uses (font and text), refreshed twice per second.
// "Growth" is measured from 5 s after start, once the font cache warmed up:
// a value that stays above zero points to a leak.

const startup = System.getMemoryStats();
if (!Number.isFinite(startup.allocsPeak) || startup.allocsPeak < startup.allocs ||
    !Number.isInteger(startup.allocationFailures) || startup.allocationFailures < 0) {
    throw new Error("Native allocator statistics are unavailable or inconsistent");
}
if (!Number.isFinite(startup.heapReserved) || startup.heapReserved < startup.heapAllocated ||
    !Number.isFinite(startup.heapOverhead) || startup.heapOverhead < 0 ||
    !Number.isFinite(startup.heapFree) || startup.heapFree < startup.heapTopFree ||
    !Number.isInteger(startup.heapFreeChunks) || startup.heapFreeChunks < 0 ||
    startup.heapNonTopFree !== startup.heapFree - startup.heapTopFree) {
    throw new Error("Allocator heap statistics are unavailable or inconsistent");
}
console.log("MEMORY_STATS: PASS native peak=" + startup.allocsPeak +
    " allocation failures=" + startup.allocationFailures);
console.log("HEAP_BREAKDOWN: PASS arena=" + startup.heapReserved +
    " allocated=" + startup.heapAllocated + " overhead=" + startup.heapOverhead +
    " free=" + startup.heapFree + " chunks=" + startup.heapFreeChunks +
    " nonTopFree=" + startup.heapNonTopFree);
const requestedNativeHeadroom = Math.floor(System.getFreeMemory() / 4);
const adjustedJsLimit = System.setNativeMemoryHeadroom(requestedNativeHeadroom);
const adjusted = System.getMemoryStats();
if (adjusted.jsLimit !== adjustedJsLimit || adjusted.jsHeap > adjusted.jsLimit) {
    throw new Error("QuickJS/native memory budget did not update consistently");
}
let excessiveRejected = false, negativeRejected = false;
try { System.setNativeMemoryHeadroom(System.getFreeMemory()); }
catch (e) { excessiveRejected = true; }
try { System.setNativeMemoryHeadroom(-1); }
catch (e) { negativeRejected = true; }
if (!excessiveRejected || !negativeRejected || System.getMemoryStats().jsLimit !== adjustedJsLimit) {
    throw new Error("Invalid native headroom changed the active JS budget");
}
console.log("MEMORY_BUDGET: PASS native headroom=" + requestedNativeHeadroom +
    " JS limit=" + adjustedJsLimit + " invalid requests rejected");
const ramSize = System.getCPUInfo().RAMSize;

const font = new Font();
const WHITE = Color.new(255, 255, 255);
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
        [WHITE, "  Heap peak        " + kb(now.allocsPeak) + "   failures " + now.allocationFailures],
        [WHITE, "  Heap arena       " + kb(now.heapReserved) + "   allocated " + kb(now.heapAllocated)],
        [WHITE, "  Heap overhead    " + kb(now.heapOverhead) + "   free " + kb(now.heapFree)],
        [WHITE, "  Free chunks      " + now.heapFreeChunks + "   top " + kb(now.heapTopFree)],
        [WHITE, "  Non-top free     " + kb(now.heapNonTopFree) + "   (fragmentation signal)"],
        [WHITE, "  JS limit         " + kb(now.jsLimit) + "   objects " + now.jsObjects],
        [WHITE, "  Used now         " + kb(now.used) + "   " + pct(now.used, ramSize)],
        [WHITE, "  Peak used        " + kb(peakUsed)],
        [WHITE, "  Free now         " + kb(System.getFreeMemory())],
        [WHITE, "Growth" + growth(now, Loop.getRealElapsedTime())],
        [YELLOW, "VRAM                   " + mb(vramTotal)],
        [WHITE, "  Used             " + kb(vramUsed) + "   " + pct(vramUsed, vramTotal)],
        [WHITE, "    static         " + kb(Screen.getMemoryStats(Screen.VRAM_USED_STATIC))],
        [WHITE, "    dynamic        " + kb(Screen.getMemoryStats(Screen.VRAM_USED_DYNAMIC))],
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
