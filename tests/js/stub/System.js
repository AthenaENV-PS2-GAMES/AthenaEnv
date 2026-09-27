/* Host stand-in for System's memory counters; __memoryStatsCalls counts the heap walks. */
const MB = 1024 * 1024;
export let __memoryStatsCalls = 0;
export function getMemoryStats() {
    __memoryStatsCalls++;
    return { core: 4 * MB, nativeStack: MB, allocs: 6 * MB, used: 12 * MB, jsHeap: 2 * MB,
        jsLimit: 8 * MB, jsObjects: 4321 };
}
export function getUsedMemory() { return 12 * MB; }
export function getFreeMemory() { return 18 * MB; }
export function getMilliseconds() { return Date.now(); }
