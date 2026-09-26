/*
 * Deep recursion must throw a catchable "stack overflow" on the main thread
 * (128 KB stack) and on JavaScript threads (their own, smaller stack),
 * instead of running past the real stack and corrupting memory.
 */
let passed = 0, failed = 0;

function check(name, condition) {
    if (condition) passed++;
    else { failed++; console.log("[FAIL] " + name); }
}

function deep(n) {
    return deep(n + 1) + 1;
}

function overflowMessage() {
    try {
        deep(0);
    } catch (error) {
        return String(error);
    }
    return "no exception";
}

// Main thread, twice: the runtime must still be usable after the first overflow.
for (let i = 0; i < 2; i++) {
    const message = overflowMessage();
    check("main thread overflow " + i + " is catchable: " + message, /stack overflow/.test(message));
}

// A JavaScript thread with the default and with the minimum stack.
for (const stack of [undefined, 16384]) {
    let message = null;
    const thread = Thread.new(() => { message = overflowMessage(); }, "Deep recursion", stack);
    Thread.start(thread);
    for (let waited = 0; message === null && waited < 3000; waited += 10) System.sleep(10);
    check(`thread (${stack || "default"} stack) overflow is catchable: ${message}`,
        message !== null && /stack overflow/.test(message));
    Thread.destroy(thread);
}

// Ordinary recursion still has room.
function depth(n) { return n === 0 ? 0 : depth(n - 1) + 1; }
check("1000 frames on the main thread", depth(1000) === 1000);

console.log(`Result: ${passed} passed, ${failed} failed`);
