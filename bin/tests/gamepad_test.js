// Gamepad module test suite.
//
// Runs without user interaction. API contract checks always run; checks that
// need hardware (a controller, a multitap, a DualShock 3/4 on USB or
// Bluetooth) are reported as SKIP when it is not found.
// The suite enables every optional driver after checking they start disabled.
// For the guided button/stick/rumble checks run gamepad_interactive_test.js.

// IOP.reset() also restarts drivers used by the host console and file I/O.
// Enable only when the script is not loaded from a device that needs them.
const RUN_IOP_RESET_TEST = false;

// Frames to wait for controllers to be detected and configured (~3 s NTSC).
const DETECT_FRAMES = 180;
// Longest acceptable Gamepad.update(), in ms. The old module could block for
// up to 1 s inside waitPadReady(); the new one must never stall the frame.
const MAX_UPDATE_MS = 4;
// The update after enabling the drivers loads mtapman, ds34usb and ds34bt.
const MAX_FIRST_UPDATE_MS = 1000;

let passed = 0;
let failed = 0;
let skipped = 0;

function pass(name) {
    console.log("[PASS] " + name);
    passed++;
}

function fail(name, error) {
    console.log("[FAIL] " + name + ": " + error);
    failed++;
}

function skip(name, reason) {
    console.log("[SKIP] " + name + ": " + reason);
    skipped++;
}

// Name of the running test, so a slow update can be traced to it.
let currentTest = "(top level)";

function test(name, callback) {
    currentTest = name;
    try {
        callback();
        pass(name);
    } catch (error) {
        fail(name, error);
    }
}

function expectThrow(name, errorType, callback) {
    test(name, function() {
        try {
            callback();
        } catch (error) {
            if (!(error instanceof errorType))
                throw new Error("expected " + errorType.name + ", got " + error);
            return;
        }
        throw new Error("expected an exception");
    });
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
}

let slowestUpdate = 0;
let slowestWhere = "";
// Duration of the Gamepad.update() in the last step(), without the vblank wait.
let lastUpdate = 0;

function step() {
    const start = System.getMilliseconds();
    Gamepad.update();
    const elapsed = System.getMilliseconds() - start;
    lastUpdate = elapsed;
    if (elapsed > slowestUpdate) {
        slowestUpdate = elapsed;
        slowestWhere = currentTest;
    }
    Screen.waitVblankStart();
}

// Updates until `condition()` holds or `frames` elapse. Returns the result.
function waitFor(frames, condition) {
    for (let i = 0; i < frames; i++) {
        step();
        if (condition()) return true;
    }
    return false;
}

function describe(player) {
    return "player " + player.index + " (" + player.connection +
        (player.connection === "port" ? " " + player.port + "." + player.slot : "") + ")";
}

// --- Module shape ---------------------------------------------------------

test("module functions are exported", function() {
    for (const name of ["update", "player", "connectedPlayers", "findJustPressed",
        "configure", "drivers", "hasMultitap"])
        assert(typeof Gamepad[name] === "function", name + " is missing");
});

test("removed legacy exports are gone", function() {
    for (const name of ["connectedCount", "connectedPorts", "MODE_ANALOG", "STATE_STABLE"])
        assert(!(name in Gamepad), name + " is still exported");
});

test("button constants match libpad", function() {
    const expected = {
        SELECT: 0x0001, L3: 0x0002, R3: 0x0004, START: 0x0008,
        UP: 0x0010, RIGHT: 0x0020, DOWN: 0x0040, LEFT: 0x0080,
        L2: 0x0100, R2: 0x0200, L1: 0x0400, R1: 0x0800,
        TRIANGLE: 0x1000, CIRCLE: 0x2000, CROSS: 0x4000, SQUARE: 0x8000,
    };
    for (const name of Object.keys(expected))
        assert(Gamepad[name] === expected[name], name + " = " + Gamepad[name]);
});

test("type constants are defined and distinct", function() {
    assert(Gamepad.TYPE_NONE === 0, "TYPE_NONE");
    assert(Gamepad.TYPE_DIGITAL === 4, "TYPE_DIGITAL");
    assert(Gamepad.TYPE_ANALOG === 5, "TYPE_ANALOG");
    assert(Gamepad.TYPE_DUALSHOCK === 7, "TYPE_DUALSHOCK");
    const types = Object.keys(Gamepad).filter(k => k.startsWith("TYPE_")).map(k => Gamepad[k]);
    assert(new Set(types).size === types.length, "duplicate TYPE_* values");
    assert(types.includes(Gamepad.TYPE_DUALSHOCK3) && types.includes(Gamepad.TYPE_DUALSHOCK4),
        "DualShock 3/4 types missing");
});

test("MAX_PLAYERS covers two multitaps", function() {
    assert(Gamepad.MAX_PLAYERS === 8, "MAX_PLAYERS " + Gamepad.MAX_PLAYERS);
});

// --- Singleton players ----------------------------------------------------

test("player() always returns the same object", function() {
    for (let i = 0; i < Gamepad.MAX_PLAYERS; i++)
        assert(Gamepad.player(i) === Gamepad.player(i), "player " + i + " differs");
    assert(Gamepad.player(0) !== Gamepad.player(1), "players share an object");
});

test("players array holds the same objects", function() {
    assert(Gamepad.players.length === Gamepad.MAX_PLAYERS, "length " + Gamepad.players.length);
    Gamepad.players.forEach((player, i) => {
        assert(player === Gamepad.player(i), "players[" + i + "]");
        assert(player.index === i, "index " + player.index);
    });
});

expectThrow("players array cannot be replaced", TypeError, function() {
    Gamepad.players[0] = {};
});

expectThrow("players array cannot grow", TypeError, function() {
    Gamepad.players.push({});
});

test("player has a descriptive tag", function() {
    assert(String(Gamepad.player(0)) === "[object GamepadPlayer]", String(Gamepad.player(0)));
});

test("removed legacy player members are gone", function() {
    const player = Gamepad.player(0);
    for (const name of ["lx", "ly", "rx", "ry", "state", "mode", "setMode",
        "pressureEnabled", "rumbleSupported"])
        assert(!(name in player), name + " is still defined");
});

expectThrow("player getters reject foreign objects", TypeError, function() {
    const proto = Object.getPrototypeOf(Gamepad.player(0));
    Object.getOwnPropertyDescriptor(proto, "connected").get.call({});
});

expectThrow("player methods reject foreign objects", TypeError, function() {
    Gamepad.player(0).pressed.call({}, Gamepad.CROSS);
});

// --- Drivers and update() -------------------------------------------------

const DRIVERS = ["multitap", "usb", "bluetooth"];

test("optional drivers start disabled", function() {
    const drivers = Gamepad.drivers();
    for (const name of DRIVERS) {
        assert(drivers[name] && drivers[name].enabled === false, name + " enabled by default");
        assert(drivers[name].ready === false, name + " ready without being enabled");
    }
});

test("first update() loads only padman", function() {
    assert(Gamepad.update() === undefined, "update returned a value");
    const drivers = Gamepad.drivers();
    const loaded = DRIVERS.filter(name => drivers[name].ready);
    assert(loaded.length === 0, "loaded without being enabled: " + loaded.join(", "));
});

test("enabled drivers load on the next update()", function() {
    Gamepad.configure({ multitap: true, usb: true, bluetooth: true });
    let drivers = Gamepad.drivers();
    assert(DRIVERS.every(name => drivers[name].enabled && !drivers[name].ready),
        "configure() should only record the request");

    const start = System.getMilliseconds();
    Gamepad.update();
    const elapsed = System.getMilliseconds() - start;
    console.log("       loading update: " + elapsed.toFixed(1) + " ms");
    assert(elapsed < MAX_FIRST_UPDATE_MS, "took " + elapsed.toFixed(1) + " ms");

    drivers = Gamepad.drivers();
    const failedDrivers = DRIVERS.filter(name => !drivers[name].ready);
    assert(failedDrivers.length === 0, "not ready: " + failedDrivers.join(", "));
});

test("configure() changes only the given drivers", function() {
    Gamepad.configure({ bluetooth: false });
    let drivers = Gamepad.drivers();
    assert(!drivers.bluetooth.enabled && !drivers.bluetooth.ready, "bluetooth still active");
    assert(drivers.multitap.enabled && drivers.usb.enabled, "other drivers changed");
    Gamepad.configure({ bluetooth: true });
    step();
    drivers = Gamepad.drivers();
    assert(drivers.bluetooth.enabled && drivers.bluetooth.ready, "bluetooth not restored");
});

test("update() can be called every frame", function() {
    for (let i = 0; i < 30; i++) step();
});

test("connectedPlayers() matches player.connected", function() {
    const connected = Gamepad.connectedPlayers();
    assert(Array.isArray(connected), "connectedPlayers is not an array");
    for (const player of Gamepad.players)
        assert(connected.includes(player) === player.connected, "player " + player.index);
    for (let i = 1; i < connected.length; i++)
        assert(connected[i - 1].index < connected[i].index, "not in index order");
});

test("findJustPressed() returns a player or null", function() {
    const found = Gamepad.findJustPressed(Gamepad.CROSS);
    assert(found === null || Gamepad.players.includes(found), "unexpected value " + found);
});

test("hasMultitap() returns booleans", function() {
    assert(typeof Gamepad.hasMultitap(0) === "boolean" && typeof Gamepad.hasMultitap(1) === "boolean",
        "not a boolean");
});

// --- Argument validation --------------------------------------------------

expectThrow("update rejects arguments", TypeError, () => Gamepad.update(1));
expectThrow("player requires an index", TypeError, () => Gamepad.player());
expectThrow("player rejects a string index", TypeError, () => Gamepad.player("0"));
expectThrow("player rejects index MAX_PLAYERS", RangeError, () => Gamepad.player(Gamepad.MAX_PLAYERS));
expectThrow("player rejects index -1", RangeError, () => Gamepad.player(-1));
expectThrow("player rejects a fractional index", RangeError, () => Gamepad.player(0.5));
expectThrow("player rejects NaN", RangeError, () => Gamepad.player(NaN));
expectThrow("connectedPlayers rejects arguments", TypeError, () => Gamepad.connectedPlayers(0));
expectThrow("findJustPressed rejects mask 0", RangeError, () => Gamepad.findJustPressed(0));
expectThrow("configure requires options", TypeError, () => Gamepad.configure());
expectThrow("configure rejects a non-object", TypeError, () => Gamepad.configure(true));
expectThrow("configure rejects non-boolean options", TypeError, () => Gamepad.configure({ usb: 1 }));
expectThrow("drivers rejects arguments", TypeError, () => Gamepad.drivers(1));
expectThrow("hasMultitap rejects port 2", RangeError, () => Gamepad.hasMultitap(2));
expectThrow("hasMultitap requires a port", TypeError, () => Gamepad.hasMultitap());

const p0 = Gamepad.player(0);

expectThrow("pressed requires a mask", TypeError, () => p0.pressed());
expectThrow("pressed rejects extra arguments", TypeError, () => p0.pressed(1, 2));
expectThrow("pressed rejects a string mask", TypeError, () => p0.pressed("CROSS"));
expectThrow("pressed rejects mask 0", RangeError, () => p0.pressed(0));
expectThrow("pressed rejects masks above 16 bits", RangeError, () => p0.pressed(0x10000));
expectThrow("justPressed rejects negative masks", RangeError, () => p0.justPressed(-1));
expectThrow("justReleased requires a mask", TypeError, () => p0.justReleased());
expectThrow("pressure rejects button combinations", RangeError,
    () => p0.pressure(Gamepad.L1 | Gamepad.R1));
expectThrow("leftStick rejects arguments", TypeError, () => p0.leftStick(1));
expectThrow("rumble requires a strength", TypeError, () => p0.rumble());
expectThrow("rumble rejects a boolean strength", TypeError, () => p0.rumble(true));
expectThrow("rumble rejects strength above 1", RangeError, () => p0.rumble(1.5));
expectThrow("rumble rejects negative weak motor", RangeError, () => p0.rumble(0, -0.1));
expectThrow("rumble rejects a fractional duration", RangeError, () => p0.rumble(1, 0, 10.5));
expectThrow("rumble rejects a negative duration", RangeError, () => p0.rumble(1, 0, -1));
expectThrow("rumble rejects extra arguments", TypeError, () => p0.rumble(1, 0, 10, 1));
expectThrow("stopRumble rejects arguments", TypeError, () => p0.stopRumble(0));
expectThrow("setAnalog requires a boolean", TypeError, () => p0.setAnalog(1));
expectThrow("setAnalog requires a boolean lock", TypeError, () => p0.setAnalog(true, "yes"));
expectThrow("pairBluetooth rejects arguments", TypeError, () => p0.pairBluetooth(1));
expectThrow("deadzone rejects negative values", RangeError, () => { p0.deadzone = -0.1; });
expectThrow("deadzone rejects values above 0.95", RangeError, () => { p0.deadzone = 1; });
expectThrow("deadzone rejects strings", TypeError, () => { p0.deadzone = "0.2"; });

test("deadzone round-trips and is per player", function() {
    const p1 = Gamepad.player(1);
    p0.deadzone = 0.25;
    assert(Math.abs(p0.deadzone - 0.25) < 1e-6, "got " + p0.deadzone);
    assert(Math.abs(p1.deadzone - 0.15) < 1e-6, "player 1 changed to " + p1.deadzone);
    p0.deadzone = 0.15;
});

// --- Snapshot values ------------------------------------------------------

function checkSnapshotRanges(player) {
    for (const stick of [player.leftStick(), player.rightStick()]) {
        assert(stick.x >= -1 && stick.x <= 1 && stick.y >= -1 && stick.y <= 1,
            "stick out of range: " + JSON.stringify(stick));
        assert(Math.hypot(stick.x, stick.y) <= 1.0001, "stick magnitude above 1");
    }
    assert(player.buttons >= 0 && player.buttons <= 0xFFFF, "buttons " + player.buttons);
    for (const button of [Gamepad.CROSS, Gamepad.L2, Gamepad.START]) {
        const value = player.pressure(button);
        assert(value >= 0 && value <= 1, "pressure " + value);
    }
}

// Let every controller that is plugged in be detected and bound.
waitFor(DETECT_FRAMES, () => false);

for (const player of Gamepad.players) {
    if (player.connected) {
        test(describe(player) + " snapshot values are in range", function() {
            checkSnapshotRanges(player);
            assert(["port", "usb", "bluetooth"].includes(player.connection),
                "connection " + player.connection);
            if (player.connection === "port") {
                assert(player.port === 0 || player.port === 1, "port " + player.port);
                assert(player.slot >= 0 && player.slot <= 3, "slot " + player.slot);
                assert(player.slot === 0 || Gamepad.hasMultitap(player.port),
                    "slot without a multitap");
            } else {
                assert(player.port === -1 && player.slot === -1, "port/slot set off-port");
            }
        });
        continue;
    }
    test("player " + player.index + " reads neutral while empty", function() {
        checkSnapshotRanges(player);
        assert(player.connection === null, "connection " + player.connection);
        assert(player.port === -1 && player.slot === -1, "port/slot not -1");
        assert(player.buttons === 0 && player.previousButtons === 0, "buttons not cleared");
        assert(player.type === Gamepad.TYPE_NONE, "type " + player.type);
        assert(!player.analog, "analog while empty");
        const stick = player.leftStick();
        assert(stick.x === 0 && stick.y === 0, "stick not centered");
        assert(!player.pressed(Gamepad.CROSS), "CROSS reported as pressed");
        assert(player.pressure(Gamepad.CROSS) === 0, "pressure not zero");
        assert(!player.hasRumble && !player.hasPressure, "capabilities not cleared");
        assert(!player.justConnected && !player.justDisconnected, "connection edge while empty");
        assert(player.rumble(1, 1, 100) === undefined, "rumble returned a value");
    });
    expectThrow("player " + player.index + " cannot pair without a USB controller", TypeError,
        () => player.pairBluetooth());
}

test("players are bound in device order", function() {
    const connected = Gamepad.connectedPlayers();
    // All controllers were present before the first bind, except multitap slots
    // (found on the first multitap scan) and DualShock 3/4 (checked every 30 updates).
    const onPorts = connected.filter(p => p.connection === "port" && p.slot === 0);
    for (let i = 1; i < onPorts.length; i++)
        assert(onPorts[i - 1].port < onPorts[i].port, "port 2 bound before port 1");
    assert(connected.every((p, i) => p.index === i), "players are not packed from 0");
});

// --- DualShock 2 on a controller port -------------------------------------

const pad = Gamepad.connectedPlayers().find(p => p.connection === "port") || null;

if (!pad) {
    skip("controller port", "no controller on port 1 or 2");
} else {
    const name = describe(pad);
    pass("controller detected: " + name + ", type " + pad.type);

    test(name + " justConnected is a one-update edge", function() {
        step();
        assert(!pad.justConnected, "justConnected still true a frame later");
    });

    test(name + " cannot pair over Bluetooth", function() {
        let threw = false;
        try { pad.pairBluetooth(); } catch (error) { threw = error instanceof TypeError; }
        assert(threw, "pairBluetooth did not throw TypeError");
    });

    const dualshock = waitFor(DETECT_FRAMES, () => pad.type === Gamepad.TYPE_DUALSHOCK && pad.analog);
    if (!dualshock) {
        skip(name + " DualShock configuration",
            "type " + pad.type + ", analog " + pad.analog + " (not a DualShock 2?)");
    } else {
        pass(name + " is a DualShock 2 in analog mode");

        if (waitFor(DETECT_FRAMES, () => pad.hasRumble))
            pass(name + " vibration motors mapped");
        else
            skip(name + " vibration motors", "padInfoAct reported no actuators");

        if (waitFor(DETECT_FRAMES, () => pad.hasPressure))
            pass(name + " pressure mode enabled");
        else
            skip(name + " pressure mode", "controller does not support pressure buttons");

        test(name + " rumble with duration does not block", function() {
            const start = System.getMilliseconds();
            pad.rumble(0.5, 1, 300);
            pad.stopRumble();
            pad.rumble(0.5, 0, 100);
            assert(System.getMilliseconds() - start < MAX_UPDATE_MS, "rumble() blocked");
            waitFor(20, () => false);
        });

        test(name + " setAnalog(false) keeps the device type", function() {
            pad.setAnalog(false);
            assert(waitFor(DETECT_FRAMES, () => !pad.analog),
                "still analog after " + DETECT_FRAMES + " frames");
            assert(pad.type === Gamepad.TYPE_DUALSHOCK, "type changed to " + pad.type);
            const stick = pad.leftStick();
            assert(stick.x === 0 && stick.y === 0, "digital mode reported stick values");
        });

        test(name + " setAnalog(true) restores analog and rumble", function() {
            pad.setAnalog(true);
            assert(waitFor(DETECT_FRAMES, () => pad.analog), "did not return to analog");
            assert(waitFor(DETECT_FRAMES, () => pad.hasRumble), "rumble not remapped");
        });
    }

    test(name + " stays connected across updates", function() {
        assert(!waitFor(60, () => !pad.connected || pad.justDisconnected), "controller dropped");
    });
}

// --- Multitap -------------------------------------------------------------

const tapped = [0, 1].filter(port => Gamepad.hasMultitap(port));

if (tapped.length === 0) {
    skip("multitap", "no multitap detected on port 1 or 2");
} else {
    pass("multitap detected on port(s) " + tapped.map(p => p + 1).join(", "));

    test("multitap slots map to distinct players", function() {
        const onTaps = Gamepad.connectedPlayers().filter(p => p.connection === "port" &&
            tapped.includes(p.port));
        const slots = new Set(onTaps.map(p => p.port + "." + p.slot));
        assert(slots.size === onTaps.length, "two players share a slot");
        console.log("       " + onTaps.map(describe).join(", "));
    });

    test("multitap controllers are configured", function() {
        const onTaps = Gamepad.connectedPlayers().filter(p => p.connection === "port" &&
            tapped.includes(p.port) && p.type === Gamepad.TYPE_DUALSHOCK);
        assert(waitFor(DETECT_FRAMES, () => onTaps.every(p => p.analog && p.hasRumble)),
            "not all DualShock 2 on the multitap reached analog + rumble");
    });

    test("disabling the multitap releases its extra slots", function() {
        Gamepad.configure({ multitap: false });
        waitFor(3, () => false);
        const extra = Gamepad.connectedPlayers().filter(p => p.connection === "port" && p.slot > 0);
        const stillTapped = [0, 1].some(port => Gamepad.hasMultitap(port));
        Gamepad.configure({ multitap: true });
        assert(extra.length === 0, extra.length + " slot players still bound");
        assert(!stillTapped, "hasMultitap still true");
        assert(waitFor(DETECT_FRAMES, () => tapped.every(port => Gamepad.hasMultitap(port))),
            "multitap not detected again");
    });
}

// --- DualShock 3/4 ---------------------------------------------------------

const ds34Types = [Gamepad.TYPE_DUALSHOCK3, Gamepad.TYPE_DUALSHOCK4];

for (const connection of ["usb", "bluetooth"]) {
    const player = Gamepad.connectedPlayers().find(p => p.connection === connection);
    if (!player) {
        skip("DualShock 3/4 over " + connection, "none connected");
        continue;
    }
    const name = describe(player);

    test(name + " reports a DualShock 3/4", function() {
        assert(ds34Types.includes(player.type), "type " + player.type);
        assert(player.analog, "not analog");
        assert(player.hasRumble, "no rumble");
        assert(player.hasPressure === (player.type === Gamepad.TYPE_DUALSHOCK3),
            "hasPressure " + player.hasPressure);
    });

    test(name + " reads never block the frame", function() {
        let worst = 0;
        for (let i = 0; i < 60; i++) {
            step();
            worst = Math.max(worst, lastUpdate);
        }
        assert(worst <= MAX_UPDATE_MS, "slowest update " + worst.toFixed(2) + " ms");
    });

    test(name + " rumble does not block", function() {
        const start = System.getMilliseconds();
        player.rumble(0.6, 0.6, 200);
        assert(System.getMilliseconds() - start < MAX_UPDATE_MS, "rumble() blocked");
        waitFor(20, () => false);
        player.stopRumble();
    });

    if (connection === "bluetooth") {
        expectThrow(name + " cannot pair itself", TypeError, () => player.pairBluetooth());
    }
}

test("update() never blocked the frame", function() {
    console.log("       slowest update: " + slowestUpdate.toFixed(2) + " ms, during: " + slowestWhere);
    assert(slowestUpdate <= MAX_UPDATE_MS,
        "slowest update took " + slowestUpdate.toFixed(2) + " ms during \"" + slowestWhere + "\"");
});

// --- IOP reset recovery (opt-in) -----------------------------------------

if (!RUN_IOP_RESET_TEST) {
    skip("recovery after IOP.reset()", "RUN_IOP_RESET_TEST is false");
} else {
    test("update() recovers after IOP.reset()", function() {
        const before = Gamepad.connectedPlayers().length;
        IOP.reset();
        // Drivers were unloaded by the reset; this must reload and rebind them.
        Gamepad.update();
        const drivers = Gamepad.drivers();
        assert(drivers.multitap.ready && drivers.usb.ready && drivers.bluetooth.ready,
            "drivers not reloaded");
        if (before)
            assert(waitFor(DETECT_FRAMES, () => Gamepad.connectedPlayers().length === before),
                "expected " + before + " controllers again");
    });
}

console.log("Result: " + passed + " passed, " + failed + " failed, " + skipped + " skipped");
if (failed !== 0) throw new Error("Gamepad tests failed");
