/* Host stand-in for System's memory counters; __memoryStatsCalls counts the heap walks. */
const MB = 1024 * 1024;
export let __memoryStatsCalls = 0;
export function setNativeMemoryHeadroom(bytes) {
    if (!Number.isInteger(bytes) || bytes < 0 || bytes > 32 * MB) throw new RangeError("invalid native headroom");
    return 8 * MB - bytes;
}
export function getMemoryStats() {
    __memoryStatsCalls++;
    return { core: 4 * MB, nativeStack: MB, allocs: 6 * MB, used: 12 * MB, allocsPeak: 7 * MB, allocationFailures: 0,
        heapReserved: 8 * MB, heapAllocated: 6 * MB, heapOverhead: 128 * 1024,
        heapFree: 2 * MB, heapFreeChunks: 4, heapTopFree: MB, heapNonTopFree: MB, jsHeap: 2 * MB,
        jsLimit: 8 * MB, jsObjects: 4321 };
}
export function getUsedMemory() { return 12 * MB; }
export function getFreeMemory() { return 18 * MB; }
export function getMilliseconds() { return Date.now(); }
