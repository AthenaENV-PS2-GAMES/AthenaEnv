let worker_started = false;
let worker = Thread.new(function() {
    worker_started = true;
}, "Timer Poll Worker");

if (Thread.start(worker) < 0) {
    throw new Error("failed to start timer poll worker");
}

setTimeout(function() {
    let failure = null;
    if (!worker_started) {
        failure = new Error("worker did not run while the event loop waited for a timer");
    }

    Thread.destroy(worker);
    if (failure) throw failure;

    console.log("[PASS] Worker runs while js_os_poll waits for a timer");
    console.log("Result: 1 passed, 0 failed");
}, 200);
