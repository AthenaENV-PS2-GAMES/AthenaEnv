let passed = 0;
let failed = 0;

function pass(name) {
    console.log("[PASS] " + name);
    passed++;
}

function fail(name, error) {
    console.log("[FAIL] " + name + ": " + error);
    failed++;
}

function test(name, callback) {
    try {
        callback();
        pass(name);
    } catch (error) {
        fail(name, error);
    }
}

function expectThrow(name, callback) {
    test(name, function() {
        let threw = false;
        try {
            callback();
        } catch (error) {
            threw = true;
        }
        if (!threw) throw new Error("expected an exception");
    });
}

test("Thread exports are available", function() {
    if (typeof Thread.new !== "function") throw new Error("new is missing");
    if (typeof Thread.start !== "function") throw new Error("start is missing");
    if (typeof Thread.stop !== "function") throw new Error("stop is missing");
    if (typeof Thread.getId !== "function") throw new Error("getId is missing");
    if (typeof Thread.getName !== "function") throw new Error("getName is missing");
    if (typeof Thread.setName !== "function") throw new Error("setName is missing");
    if (typeof Thread.getStatus !== "function") throw new Error("getStatus is missing");
    if (typeof Thread.destroy !== "function") throw new Error("destroy is missing");
    if (typeof Thread.list !== "function") throw new Error("list is missing");
    if (typeof Thread.kill !== "function") throw new Error("kill is missing");
});

test("Thread.new creates valid thread and reports metadata", function() {
    const thread = Thread.new(function() {}, "Test Worker Thread");
    if (typeof thread !== "object" || thread === null) throw new Error("thread is not an object");

    const id = Thread.getId(thread);
    if (typeof id !== "number" || id < 0) throw new Error("invalid thread id: " + id);

    const name = Thread.getName(thread);
    if (name !== "Test Worker Thread") throw new Error("unexpected thread name: " + name);

    Thread.setName(thread, "Renamed Worker");
    if (Thread.getName(thread) !== "Renamed Worker") throw new Error("thread name was not updated");

    Thread.destroy(thread);
});

test("Thread instance methods are available on prototype", function() {
    const thread = Thread.new(function() {}, "Instance Test");
    if (typeof thread.start !== "function") throw new Error("thread.start is missing");
    if (typeof thread.stop !== "function") throw new Error("thread.stop is missing");
    if (typeof thread.destroy !== "function") throw new Error("thread.destroy is missing");
    if (typeof thread.getId !== "function") throw new Error("thread.getId is missing");
    if (typeof thread.getName !== "function") throw new Error("thread.getName is missing");
    if (typeof thread.setName !== "function") throw new Error("thread.setName is missing");

    thread.destroy();
});

test("Started worker runs native-yielding callback and stops cooperatively", function() {
    let started = false;
    const thread = Thread.new(function() {
        started = true;
        while (true) {
            System.sleep(1);
        }
    }, "Cooperative Worker");

    if (Thread.start(thread) < 0) throw new Error("failed to start worker");
    System.sleep(50);
    if (!started) throw new Error("worker callback did not start");

    if (Thread.stop(thread) < 0) throw new Error("failed to request stop");
    Thread.destroy(thread);
});

test("Worker with periodic native polling is cancelled by interrupt handler", function() {
    let started = false;
    let iterations = 0;
    const thread = Thread.new(function() {
        started = true;
        while (true) {
            iterations = iterations + 1;
            if ((iterations % 100) === 0) System.sleep(1);
        }
    }, "Polled Worker");

    if (Thread.start(thread) < 0) throw new Error("failed to start polled worker");
    System.sleep(50);
    if (!started || iterations === 0) {
        Thread.stop(thread);
        Thread.destroy(thread);
        throw new Error("polled worker did not execute");
    }

    if (Thread.stop(thread) < 0) throw new Error("failed to request polled worker stop");
    Thread.destroy(thread);
});

test("Mutex.lock releases the runtime gate while blocked", function() {
    const mutex = Mutex.new();
    let acquired = false;

    if (Mutex.lock(mutex) < 0) {
        Mutex.destroy(mutex);
        throw new Error("main thread failed to lock mutex");
    }

    const thread = Thread.new(function() {
        Mutex.lock(mutex);
        acquired = true;
        Mutex.unlock(mutex);
    }, "Blocked Mutex Worker");

    if (Thread.start(thread) < 0) {
        Mutex.unlock(mutex);
        Mutex.destroy(mutex);
        throw new Error("failed to start mutex worker");
    }

    System.sleep(50);
    if (acquired) {
        Thread.stop(thread);
        Thread.destroy(thread);
        Mutex.unlock(mutex);
        Mutex.destroy(mutex);
        throw new Error("worker acquired mutex before main released it");
    }

    if (Mutex.unlock(mutex) < 0) throw new Error("main thread failed to unlock mutex");
    Thread.destroy(thread);
    if (!acquired) throw new Error("worker did not acquire mutex after release");
    Mutex.destroy(mutex);
});

test("Mutex.destroy rejects an in-use semaphore", function() {
    const mutex = Mutex.new();
    let waiting = false;
    if (Mutex.lock(mutex) < 0) throw new Error("main thread failed to lock mutex");

    const thread = Thread.new(function() {
        waiting = true;
        Mutex.lock(mutex);
        Mutex.unlock(mutex);
    }, "Mutex Lifetime Worker");
    if (Thread.start(thread) < 0) {
        Mutex.unlock(mutex);
        Mutex.destroy(mutex);
        throw new Error("failed to start mutex lifetime worker");
    }
    System.sleep(25);
    if (!waiting) throw new Error("mutex lifetime worker did not start");

    let rejected = false;
    try {
        Mutex.destroy(mutex);
    } catch (error) {
        rejected = true;
    }
    if (!rejected) throw new Error("destroy accepted while mutex was in use");

    if (Mutex.unlock(mutex) < 0) throw new Error("main thread failed to unlock mutex");
    Thread.destroy(thread);
    Mutex.destroy(mutex);
});

test("Timer operations remain valid until explicit destruction", function() {
    const timer = Timer.new();
    Timer.pause(timer);
    Timer.reset(timer);
    Timer.resume(timer);
    if (typeof Timer.getTime(timer) !== "number")
        throw new Error("timer did not return elapsed time");
    Timer.destroy(timer);

    let rejected = false;
    try {
        Timer.getTime(timer);
    } catch (error) {
        rejected = true;
    }
    if (!rejected) throw new Error("destroyed timer remained accessible");
});

test("Timer operations remain serialized across workers", function() {
    const timer = Timer.new();
    let completed = false;
    let worker_error = null;
    const thread = Thread.new(function() {
        try {
            for (let i = 0; i < 5; i++) {
                Timer.reset(timer);
                Timer.resume(timer);
                if (typeof Timer.getTime(timer) !== "number") {
                    throw new Error("worker timer query was not numeric");
                }
                Timer.pause(timer);
            }
            completed = true;
        } catch (error) {
            worker_error = error;
        }
    }, "Timer Worker");

    if (Thread.start(thread) < 0) {
        Timer.destroy(timer);
        throw new Error("failed to start timer worker");
    }
    Thread.destroy(thread);
    if (worker_error !== null) {
        Timer.destroy(timer);
        throw worker_error;
    }
    if (!completed) {
        Timer.destroy(timer);
        throw new Error("timer worker did not complete");
    }
    Timer.destroy(timer);
});

test("Thread.kill cooperatively stops an active worker", function() {
    let started = false;
    const thread = Thread.new(function() {
        started = true;
        while (true) {
            System.sleep(1);
        }
    }, "Kill Worker");

    if (Thread.start(thread) < 0) throw new Error("failed to start kill worker");
    System.sleep(50);
    if (!started) {
        Thread.destroy(thread);
        throw new Error("kill worker did not execute");
    }

    const id = Thread.getId(thread);
    if (Thread.kill(id) < 0) throw new Error("Thread.kill failed");
    Thread.destroy(thread);
});

test("Long native sleep can finish while the main thread remains active", function() {
    let completed = false;
    const thread = Thread.new(function() {
        System.sleep(100);
        completed = true;
    }, "Long Sleep Worker");

    if (Thread.start(thread) < 0) throw new Error("failed to start sleep worker");
    System.sleep(10);
    if (completed) {
        Thread.destroy(thread);
        throw new Error("sleep worker completed too early");
    }

    Thread.destroy(thread);
    if (!completed) throw new Error("sleep worker did not finish before destroy returned");
});

test("Multiple active workers can be stopped and finalized", function() {
    const workers = [];
    for (let i = 0; i < 3; i++) {
        const worker = Thread.new(function() {
            while (true) {
                System.sleep(1);
            }
        }, "Concurrent Worker " + i);
        if (Thread.start(worker) < 0) {
            for (let j = 0; j < workers.length; j++) {
                Thread.stop(workers[j]);
                Thread.destroy(workers[j]);
            }
            Thread.destroy(worker);
            throw new Error("failed to start concurrent worker " + i);
        }
        workers.push(worker);
    }

    System.sleep(50);
    for (let i = 0; i < workers.length; i++) {
        if (Thread.stop(workers[i]) < 0) throw new Error("failed to stop worker " + i);
    }
    for (let i = 0; i < workers.length; i++) {
        Thread.destroy(workers[i]);
    }
});

test("Runtime shutdown waits for an active worker", function() {
    let started = false;
    const thread = Thread.new(function() {
        started = true;
        while (true) {
            System.sleep(1);
        }
    }, "Shutdown Worker");

    if (Thread.start(thread) < 0) throw new Error("failed to start shutdown worker");
    System.sleep(25);
    if (!started) throw new Error("shutdown worker did not execute");
    /* Leave the worker active so run_script() exercises wait_all(). */
});

test("Thread.list returns array of tasks", function() {
    const list = Thread.list();
    if (!Array.isArray(list)) throw new Error("Thread.list did not return an array");
    if (list.length === 0) throw new Error("Thread.list returned empty array");

    const first = list[0];
    if (typeof first.id !== "number") throw new Error("task id missing in list entry");
    if (typeof first.name !== "string") throw new Error("task name missing in list entry");
    if (typeof first.status !== "number") throw new Error("task status missing in list entry");
    if (typeof first.stack !== "number") throw new Error("task stack missing in list entry");
});

test("System threads cannot be killed", function() {
    const list = Thread.list();
    let systemId = -1;
    for (let i = 0; i < list.length; i++) {
        if (list[i].name.indexOf("System:") === 0 ||
            list[i].name.indexOf("AthenaEnv:") === 0 ||
            list[i].name.indexOf("Kernel:") === 0) {
            systemId = list[i].id;
            break;
        }
    }
    if (systemId < 0) throw new Error("no system thread was reported");
    if (Thread.kill(systemId) >= 0)
        throw new Error("system thread kill was accepted");
});

test("System native queries release and reacquire the runtime gate", function() {
    let completed = false;
    let worker_error = null;
    let device_count = -1;
    let directory_count = -1;
    let bdm_info = null;
    const thread = Thread.new(function() {
        try {
            const entries = System.listDir();
            if (!Array.isArray(entries)) throw new Error("System.listDir did not return an array");
            directory_count = entries.length;
            const devices = System.devices();
            if (!Array.isArray(devices)) throw new Error("System.devices did not return an array");
            device_count = devices.length;
            bdm_info = System.getBDMInfo("mass0:");
            completed = true;
        } catch (error) {
            worker_error = error;
        }
    }, "System Query Worker");

    if (Thread.start(thread) < 0) throw new Error("failed to start system query worker");
    const main_ticks = System.getTicks();
    System.sleep(10);
    if (System.getTicks() < main_ticks) {
        Thread.destroy(thread);
        throw new Error("main thread clock moved backwards");
    }

    Thread.destroy(thread);
    if (worker_error !== null) throw worker_error;
    if (!completed) throw new Error("system query worker did not complete");
    if (directory_count < 0) throw new Error("system directory result was not retained");
    if (device_count < 0) throw new Error("system device result was not retained");
    if (bdm_info !== undefined && typeof bdm_info !== "object") {
        throw new Error("unexpected BDM information result");
    }
});

expectThrow("Thread.new rejects missing arguments", function() {
    Thread.new();
});

expectThrow("Thread.new rejects non-function argument", function() {
    Thread.new("not a function");
});

expectThrow("Thread.new rejects invalid priority", function() {
    Thread.new(function() {}, "Invalid Priority", 16384, 999);
});

expectThrow("Thread.new rejects invalid stack size", function() {
    Thread.new(function() {}, "Invalid Stack", 100);
});

expectThrow("Thread operations reject values from another type", function() {
    Thread.start({});
});

expectThrow("Thread operations reject missing arguments", function() {
    Thread.start();
});

expectThrow("Thread.destroy rejects an already destroyed thread", function() {
    const thread = Thread.new(function() {}, "Destroy Test");
    Thread.destroy(thread);
    Thread.destroy(thread);
});

expectThrow("Thread operations fail after thread is destroyed", function() {
    const thread = Thread.new(function() {}, "Post Destroy Test");
    Thread.destroy(thread);
    Thread.getId(thread);
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Thread tests failed");
