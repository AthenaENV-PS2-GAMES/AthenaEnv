/*
 * Scene module: reference-counted assets (groups, sharing, release while
 * loading, failures, custom kinds, the built-in data, images and sheets
 * kinds), and the scene manager (go, push, pop, fades, the loading screen,
 * async enter, errors, queued requests, unloadFirst, real time under
 * Loop.setTimeScale(0)). Runs on PCSX2/PS2 and on the host (tests/js/run.sh).
 *
 * Most checks drive the manager by hand: without Loop.run(), Scene.update()
 * advances transitions and loading itself. A "fake" asset kind whose loads
 * the test completes controls the order of everything.
 */
import * as Loop from "Loop";

let passed = 0, failed = 0;

function check(name, condition) {
    if (condition) {
        passed++;
    } else {
        failed++;
        console.log("[FAIL] " + name);
    }
}

function throws(name, callback, type) {
    let error = null;
    try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""),
        error !== null && (!type || error instanceof type));
    return error;
}

const host = typeof globalThis.__spriteDraws === "function";
const { Assets } = Scene;
const FRAME = 1 / 60;

/* Lets promise callbacks run. */
async function flush() {
    for (let i = 0; i < 6; i++) await null;
}

/* Frames driven by hand: update and draw, then the promise callbacks. */
async function step(count = 1, dt = FRAME) {
    for (let i = 0; i < count; i++) {
        await flush();
        Scene.update(dt);
        Scene.draw(1);
        await flush();
    }
}

/* Runs Loop frames until `done()` (at most `limit`): for work that needs the Loop's systems. */
function loopUntil(done, limit = 600) {
    return new Promise(resolve => {
        let frame = 0;
        Loop.run({
            update() {
                if (done() || ++frame >= limit) {
                    Loop.stop();
                    resolve(done());
                }
            },
        });
    });
}

/* A kind whose loads finish when the test says so. */
const fake = { loads: [], freed: [], pending: new Map() };
Assets.define("fake", {
    load(path) {
        fake.loads.push(path);
        if (path.split("/").pop().startsWith("sync")) return { path };
        return new Promise((resolve, reject) => fake.pending.set(path, { resolve, reject }));
    },
    free(asset) { fake.freed.push(asset.path); },
});
function complete(path) {
    const load = fake.pending.get(path);
    fake.pending.delete(path);
    load.resolve({ path });
}
function reject(path) {
    const load = fake.pending.get(path);
    fake.pending.delete(path);
    load.reject(new Error("cannot load " + path));
}
const count = (list, value) => list.filter(item => item === value).length;

async function assets() {
    check("Scene global", typeof Scene === "function" && typeof Assets === "object" &&
        typeof Scene.go === "function");

    const group = Assets.acquire({ fake: { a: "a", b: "b" } });
    check("loading", group.progress === 0 && !group.done && group.total === 2);
    complete("a");
    await flush();
    check("half", group.progress === 0.5 && group.assets.fake.a.path === "a");
    complete("b");
    await flush();
    check("done", group.done && group.progress === 1);
    check("ready resolves with the assets", (await group.ready) === group.assets);

    // Shared: no second load, ready at once; freed after the last release.
    const again = Assets.acquire({ fake: { first: "a" } });
    check("cached asset is ready at once", again.done && again.assets.fake.first === group.assets.fake.a &&
        count(fake.loads, "a") === 1);
    group.release();
    check("still held", !fake.freed.includes("a") && fake.freed.includes("b"));
    again.release();
    again.release();
    check("freed with the last holder, once", count(fake.freed, "a") === 1);

    // Released while loading: freed when it arrives.
    const early = Assets.acquire({ fake: { c: "c" } });
    early.release();
    complete("c");
    await flush();
    check("freed on arrival", fake.freed.includes("c"));

    // Failures: the group fails, and a later acquire tries again.
    const broken = Assets.acquire({ fake: { d: "d", e: "e" } });
    reject("d");
    await flush();
    let error = null;
    try { await broken.ready; } catch (e) { error = e; }
    check("failed group", broken.failed && error && /cannot load d/.test(error.message));
    broken.release();
    complete("e");
    await flush();
    const retry = Assets.acquire({ fake: { d: "d" } });
    check("a failed asset loads again", count(fake.loads, "d") === 2);
    complete("d");
    await flush();
    retry.release();

    // One asset, released by value.
    const single = Assets.load("fake", "sync-1");
    const value = await single;
    check("load", value.path === "sync-1" && Assets.stats().byKind.fake === 1);
    check("release by value", Assets.release(value) && !Assets.release(value) &&
        fake.freed.includes("sync-1"));

    // Paths relative to a root.
    const rooted = Assets.acquire({ fake: { r: "sync-r.bin" } }, "levels/one");
    check("root", Assets.list().some(entry => entry.path === "levels/one/sync-r.bin"));
    rooted.release();

    throws("unknown kind", () => Assets.acquire({ nope: { a: "a" } }), TypeError);
    throws("bad manifest", () => Assets.acquire([]), TypeError);
    throws("bad kind table", () => Assets.acquire({ fake: "a" }), TypeError);
    throws("spec without path", () => Assets.acquire({ fake: { a: {} } }), TypeError);
    throws("define without free", () => Assets.define("x", { load() {} }), TypeError);
    await flush();
    check("nothing leaked", Assets.stats().entries === 0 || console.log(JSON.stringify(Assets.list())));
}

async function builtinKinds() {
    const paths = ["tests/sprite_sheet.json", "sprite_sheet.json", "bin/tests/sprite_sheet.json"];
    let data = null;
    for (const path of paths) {
        try { data = await Assets.load("data", path); break; } catch (e) { /* next */ }
    }
    check("data kind", data && data.meta && data.meta.image === "texture.png");
    if (data) Assets.release(data);

    const texture = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
    let image = null, sheet = null;
    for (const path of texture) {
        let error = null;
        Assets.load("images", path).then(value => image = value, e => error = e);
        await loopUntil(() => image !== null || error !== null);
        if (image) break;
    }
    check("images kind", image instanceof Image);
    for (const path of paths) {
        let error = null;
        Assets.load("sheets", path).then(value => sheet = value, e => error = e);
        await loopUntil(() => sheet !== null || error !== null);
        if (sheet) break;
    }
    check("sheets kind", sheet instanceof Sprite.Sheet && sheet.frameCount === 4);
    if (image) Assets.release(image);
    if (sheet) Assets.release(sheet);
    await flush();
    check("released", Assets.stats().entries === 0 || console.log(JSON.stringify(Assets.list())));
}

/* ---- Scenes ---------------------------------------------------------------------------------- */

const log = [];
class Base extends Scene {
    enter(assets, params) { log.push(`${this.constructor.name}.enter(${params ?? ""})`); }
    update() { log.push(`${this.constructor.name}.update`); }
    draw() { log.push(`${this.constructor.name}.draw`); }
    pause() { log.push(`${this.constructor.name}.pause`); }
    resume(result) { log.push(`${this.constructor.name}.resume(${result ?? ""})`); }
    exit() { log.push(`${this.constructor.name}.exit`); }
}
class A extends Base { static assets = { fake: { x: "x", shared: "s" } }; }
class B extends Base { static assets = { fake: { shared: "s", y: "y" } }; }
class P extends Base {}
class Q extends Base {}
class Broken extends Base { static assets = { fake: { bad: "bad" } }; }
class Dynamic extends Base { static assets(params) { return { fake: { level: `sync-level-${params}` } }; } }
let finishEnter = null;
class Slow extends Base {
    enter(assets, params) {
        super.enter(assets, params);
        return new Promise(resolve => finishEnter = resolve);
    }
}
const has = entry => log.includes(entry);

async function scenes() {
    const screens = [];
    Scene.loadingScreen = (progress, info) => screens.push([progress, info.loaded, info.total]);
    Scene.loadingDelay = 0.05;
    Scene.minLoadingTime = 0;

    // go: loads, shows the loading screen after its delay, enters.
    let first = null;
    Scene.go(A, { params: 1 }).then(scene => first = scene);
    check("busy", Scene.busy && Scene.current === null);
    await step(2);
    check("no loading screen before the delay", screens.length === 0 && !has("A.enter(1)"));
    await step(3);
    check("loading screen", screens.length > 0 && screens[screens.length - 1][0] === 0 &&
        screens[screens.length - 1][2] === 2);
    complete("x");
    await step();
    check("progress", screens[screens.length - 1][0] === 0.5);
    complete("s");
    await step();
    check("entered", has("A.enter(1)") && first instanceof A && Scene.current === first &&
        first.assets.fake.x.path === "x" && first.params === 1 && !Scene.busy);
    log.length = 0;
    await step();
    check("update and draw", has("A.update") && has("A.draw"));

    // go with a fade: the shared asset stays; the old scene is frozen during the fade-out.
    globalThis.__debugCalls = [];
    let second = null;
    Scene.go(B, { transition: "fade", duration: 0.2 }).then(scene => second = scene);
    log.length = 0;
    await step(3);
    check("fading out: drawn, not updated", has("A.draw") && !has("A.update"));
    const covers = (globalThis.__debugCalls || []).filter(c => c[0] === "rect" && c[3] === 640 && c[4] === 448);
    if (host) check("fade overlay", covers.length > 0);   /* the Draw stand-in logs draws */
    await step(5);
    check("covered while loading", !has("B.enter()") && Scene.progress === 0.5);
    complete("y");
    await step();
    check("switched", has("A.exit") && has("B.enter()") && Scene.current instanceof B);
    check("exclusive freed, shared kept and not reloaded", fake.freed.includes("x") &&
        !fake.freed.includes("s") && count(fake.loads, "s") === 1);
    log.length = 0;
    await step();
    check("fading in: updated", has("B.update") && second === null);
    await step(8);
    check("go resolves after the fade-in", second instanceof B);

    // push and pop.
    log.length = 0;
    const pushed = await (async () => { const p = Scene.push(P, { params: 2 }); await step(); return p; })();
    check("push", pushed instanceof P && has("B.pause") && has("P.enter(2)") && Scene.stack.length === 2);
    log.length = 0;
    await step();
    check("only the top updates; both draw, below first", has("P.update") && !has("B.update") &&
        log.indexOf("B.draw") < log.indexOf("P.draw"));
    log.length = 0;
    const popped = Scene.pop({ result: 7 });
    await step();
    check("pop", has("P.exit") && has("B.resume(7)") && (await popped) === Scene.current);

    // updateBelow, drawBelow false.
    Scene.push(Q, { updateBelow: true, drawBelow: false });
    await step();
    log.length = 0;
    await step();
    check("updateBelow", has("B.update") && has("Q.update") && !has("B.draw") && has("Q.draw"));
    Scene.pop();
    await step();

    // Params in the manifest.
    Scene.go(Dynamic, { params: 3 });
    await step();
    check("manifest from params", Scene.current instanceof Dynamic &&
        Scene.current.assets.fake.level.path === "sync-level-3");

    // Async enter: not drawn nor updated until it resolves.
    Scene.go(Slow);
    await step();
    log.length = 0;
    await step(4);
    check("waits for enter()", Scene.busy && !has("Slow.update") && !has("Slow.draw"));
    finishEnter();
    await step();
    log.length = 0;
    await step();
    check("runs after enter()", !Scene.busy && has("Slow.update") && has("Slow.draw"));

    // Errors: onError, and the current scene stays.
    const errors = [];
    Scene.onError = (error, SceneClass) => errors.push([error.message, SceneClass]);
    let rejected = null;
    Scene.go(Broken).catch(error => rejected = error);
    await step();
    reject("bad");
    await step();
    check("onError", errors.length === 1 && /cannot load bad/.test(errors[0][0]) && errors[0][1] === Broken);
    check("the go rejects, the scene stays", rejected !== null && Scene.current instanceof Slow);
    // Without onError, the next update throws.
    Scene.onError = null;
    Scene.go(Broken);
    await step();
    reject("bad");
    await flush();
    throws("error from update", () => Scene.update(FRAME), Error);

    // Queued requests run in order.
    log.length = 0;
    const order = [];
    Scene.go(P).then(() => order.push("P"));
    Scene.go(Q).then(() => order.push("Q"));
    await step(3);
    check("queued", order.join() === "P,Q" && Scene.current instanceof Q && has("P.exit"));

    // unloadFirst: the old scene's assets go before loading (a shared one loads again).
    Scene.go(A);
    await step();
    complete("x");
    complete("s");
    await step(2);
    const loadsOfS = count(fake.loads, "s");
    Scene.go(B, { unloadFirst: true });
    await step();
    check("unloadFirst frees first", fake.freed.includes("s") && count(fake.loads, "s") === loadsOfS + 1);
    complete("s");
    complete("y");
    await step(2);
    check("unloadFirst enters", Scene.current instanceof B);

    // Errors of the API.
    throws("go without a scene class", () => Scene.go({}), TypeError);
    throws("go with a plain function", () => Scene.go(function () {}), TypeError);
    throws("bad transition", () => Scene.go(P, { transition: "spiral" }), TypeError);
    throws("bad duration", () => Scene.go(P, { duration: -1 }), RangeError);
    throws("bad loading screen", () => { Scene.loadingScreen = 3; }, TypeError);
    throws("bad delay", () => { Scene.loadingDelay = -1; }, RangeError);

    Scene.reset();
    await flush();
    check("reset", Scene.current === null && !Scene.busy &&
        (Assets.stats().entries === 0 || console.log(JSON.stringify(Assets.list()))));
    let popError = null;
    Scene.onError = error => popError = error;
    Scene.pop();
    check("pop with no scene", popError && /no scene to pop/.test(popError.message));
    Scene.onError = null;
}

/* The manager runs on real time: a paused game (time scale 0) still changes scenes. */
async function realTime() {
    Scene.loadingScreen = null;
    const first = Scene.run(P);
    await loopUntil(() => Scene.current instanceof P);
    check("run", (await first) instanceof P);
    Loop.setTimeScale(0);
    let done = false;
    Scene.go(Q, { transition: "fade", duration: 0.1 }).then(() => done = true);
    await loopUntil(() => done, 120);
    Loop.setTimeScale(1);
    Loop.stop();
    check("transition under time scale 0", done && Scene.current instanceof Q);
    Scene.reset();
}

async function robustness() {
    Scene.loadingScreen = null;
    Scene.onError = null;

    // An enter() that throws: the error reaches the caller, the scene leaves, the manager goes on.
    class BadEnter extends Base {
        static assets = { fake: { held: "sync-held" } };
        enter() {
            this.defer(() => log.push("BadEnter.cleanup"));
            throw new Error("enter failed");
        }
    }
    log.length = 0;
    Scene.go(BadEnter);
    await flush();
    const error = throws("enter() error", () => Scene.update(FRAME), Error);
    await flush();
    check("enter() error message", error && /enter failed/.test(error.message));
    check("the failed scene leaves without exit()", Scene.current === null && has("BadEnter.cleanup") &&
        !has("BadEnter.exit") && !Scene.busy);
    check("its assets are released", fake.freed.includes("sync-held"));
    Scene.go(P);
    await step();
    check("the manager goes on", Scene.current instanceof P);

    // An exit() that throws: the switch still releases everything, then the error surfaces.
    class BadExit extends Base {
        static assets = { fake: { own: "sync-own" } };
        exit() { throw new Error("exit failed"); }
    }
    Scene.go(BadExit);
    await step();
    Scene.go(Q);
    await flush();
    throws("exit() error", () => Scene.update(FRAME), Error);
    await flush();
    check("released despite exit()", fake.freed.includes("sync-own") && !Scene.busy);
    Scene.reset();

    // Load failures name the request and the scene.
    let failure = null;
    Scene.onError = e => failure = e;
    Scene.go(Broken);
    await step();
    reject("bad");
    await step();
    check("described failure", failure && failure.message.startsWith("Scene.go(Broken): cannot load bad") &&
        failure.scene === Broken && failure.cause instanceof Error);
    Scene.onError = null;
}

async function lifetimes() {
    Scene.reset();
    // defer() and acquire(): cleaned up when the scene leaves, after exit().
    class Owner extends Base {
        enter() {
            this.defer(() => log.push("cleanup 1"));
            this.defer(() => log.push("cleanup 2"));
            this.extra = this.acquire({ fake: { more: "sync-more" } });
        }
    }
    log.length = 0;
    Scene.go(Owner);
    await step(2);
    const owner = Scene.current;
    check("acquire", owner.extra.done && owner.extra.assets.fake.more.path === "sync-more");
    Scene.go(P);
    await step();
    check("clean-ups after exit, last first", log.indexOf("Owner.exit") < log.indexOf("cleanup 2") &&
        log.indexOf("cleanup 2") < log.indexOf("cleanup 1"));
    check("acquired group released", fake.freed.includes("sync-more"));
    check("assets cleared after exit", owner.assets === null);
    throws("defer needs a function", () => owner.defer(3), TypeError);

    // replace: the top changes, the scene below stays and is not resumed.
    Scene.push(Q);
    await step();
    log.length = 0;
    const replaced = Scene.replace(Base);
    await step();
    check("replace", (await replaced) instanceof Base && has("Q.exit") && !has("P.resume()") &&
        Scene.stack.length === 2 && Scene.stack[0] instanceof P);
    Scene.reset();
    Scene.replace(P);
    await step();
    check("replace on an empty stack", Scene.current instanceof P && Scene.stack.length === 1);

    // preload: the next scene's assets load while the current one runs.
    class Next extends Base { static assets = { fake: { big: "big" } }; }
    const pre = Scene.preload(Next);
    check("preload starts", pre.progress === 0 && count(fake.loads, "big") === 1);
    complete("big");
    await flush();
    Scene.go(Next);
    await step();
    check("go finds it loaded", Scene.current instanceof Next && count(fake.loads, "big") === 1 &&
        pre.released);
    Scene.reset();
    check("reset frees it", fake.freed.includes("big"));
    const unused = Scene.preload(Next);
    Scene.reset();
    check("reset releases preloads", unused.released);
    complete("big");
    await flush();
    throws("preload of a non-scene", () => Scene.preload({}), TypeError);

    // A load released and acquired again while it runs is not loaded twice.
    const once = Assets.acquire({ fake: { again: "again" } });
    once.release();
    const twice = Assets.acquire({ fake: { again: "again" } });
    check("reused while loading", count(fake.loads, "again") === 1);
    complete("again");
    await flush();
    check("revived load is kept", twice.done && !fake.freed.includes("again"));
    twice.release();
    check("then freed", fake.freed.includes("again"));
}

/* Frees asked for while a frame is drawn wait for the next frame. */
async function deferredFrees() {
    const value = await Assets.load("fake", "sync-deferred");
    let frame = 0, freedDuringDraw = null;
    await new Promise(resolve => {
        Loop.run({
            draw() {
                frame++;
                if (frame === 1) {
                    Assets.release(value);
                    freedDuringDraw = fake.freed.includes("sync-deferred");
                }
                if (frame === 3) {
                    Loop.stop();
                    resolve();
                }
            },
        });
    });
    check("a free asked for during draw waits", freedDuringDraw === false);
    check("and runs at the next frame", fake.freed.includes("sync-deferred") &&
        Assets.stats().pendingFrees === 0);
}

async function sheetOptions() {
    const paths = ["tests/sprite_sheet.json", "sprite_sheet.json", "bin/tests/sprite_sheet.json"];
    const texture = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
    let image = null;
    for (const path of texture) {
        try { image = new Image(path); break; } catch (e) { /* next */ }
    }
    let freedByUs = false;
    const originalFree = image.free;
    image.free = function () { freedByUs = true; if (originalFree) originalFree.call(this); };
    let sheet = null;
    for (const path of paths) {
        let error = null;
        Assets.load("sheets", { path, image }).then(value => sheet = value, e => error = e);
        await loopUntil(() => sheet !== null || error !== null);
        if (sheet) break;
    }
    check("sheet with the game's image", sheet && sheet.image === image);
    const other = Assets.list().filter(entry => entry.kind === "sheets").length;
    Assets.release(sheet);
    check("the game's image is not freed", !freedByUs && other === 1);
    image.free = originalFree;
}

function runChecks() {
    Loop.run({ draw() {} });
    throws("run while the Loop runs", () => Scene.run(P), TypeError);
    Loop.stop();
}

async function fileReads() {
    const paths = ["tests/sprite_sheet.json", "sprite_sheet.json", "bin/tests/sprite_sheet.json"];
    let path = null, text = null;
    for (const candidate of paths) {
        try { text = await Thread.readFileAsync(candidate, { text: true }); path = candidate; break; }
        catch (e) { /* next */ }
    }
    check("readFileAsync text", typeof text === "string" && text.includes("slime 0.aseprite"));
    const bytes = await Thread.readFileAsync(path);
    check("readFileAsync binary", bytes instanceof ArrayBuffer && bytes.byteLength === text.length);
    let error = null;
    try { await Thread.readFileAsync(path, { maxBytes: 10 }); } catch (e) { error = e; }
    check("maxBytes", error && /larger than maxBytes/.test(error.message) && error.path === path);
    error = null;
    try { await Thread.readFileAsync("tests/no_such_file.txt"); } catch (e) { error = e; }
    check("missing file", error && /cannot open/.test(error.message));
    throws("path type", () => Thread.readFileAsync(3), TypeError);
    throws("maxBytes range", () => Thread.readFileAsync(path, { maxBytes: 0 }), RangeError);

    // The kinds that read files: text, binary, and a clear error for bad JSON.
    const t = await Assets.load("text", path);
    check("text kind", t === text);
    check("strings are released by kind and path", !Assets.release(t) && Assets.release("text", path) &&
        !Assets.release("text", path));
    const b = await Assets.load("binary", path);
    check("binary kind", b instanceof ArrayBuffer && b.byteLength === text.length);
    Assets.release(b);
    error = null;
    const notJson = path.replace("sprite_sheet.json", "texture.png");
    try { await Assets.load("data", notJson); } catch (e) { error = e; }
    check("invalid JSON names the file", error instanceof SyntaxError && error.message.includes(notJson));
    await flush();
    check("file kinds released", Assets.stats().entries === 0 || console.log(JSON.stringify(Assets.list())));
}

/* An ImageList stand-in, installed only for this test: loads finish when process() runs. */
class FakeImageList {
    constructor() {
        FakeImageList.made++;
        this.queue = [];
        this.closed = false;
    }
    load(path, options) {
        const image = {
            path, isLocked: options.upload === "lock", freed: false,
            locked() { return this.isLocked; }, lock() { this.isLocked = true; return true; },
            unlock() { this.isLocked = false; return true; }, free() { this.freed = true; },
        };
        this.queue.push({ image, options });
        return image;
    }
    process() {
        const done = this.queue.splice(0);
        for (const { image, options } of done) options.onLoad(image);
        return done.length;
    }
    cancel(image) {
        const before = this.queue.length;
        this.queue = this.queue.filter(item => item.image !== image);
        return this.queue.length !== before;
    }
    close() { this.closed = true; }
}
FakeImageList.made = 0;

/* Closes the module's ImageList (a real one, on the console, from the earlier tests). */
function closeImageList() {
    const idle = Assets.imageListIdleTime;
    Assets.imageListIdleTime = 0;
    Assets.update(1);
    Assets.imageListIdleTime = idle;
}

async function imageLists() {
    const saved = globalThis.ImageList;
    closeImageList();
    check("no ImageList open before", !Assets.stats().imageListOpen);
    globalThis.ImageList = FakeImageList;
    try {
        let first = null;
        Assets.load("images", "hero.png").then(image => first = image);
        check("queued on the ImageList", FakeImageList.made === 1 && Assets.stats().imageListOpen);
        Assets.update(FRAME);
        await flush();
        check("loaded by the pump", first && first.path === "hero.png" && !first.isLocked);

        // A cached image asked for again with a lock gets locked.
        const again = await Assets.load("images", { path: "hero.png", lock: true });
        check("reuse locks", again === first && first.isLocked);
        // Asked for with a lock while it still loads: locked when it arrives.
        let second = null;
        Assets.load("images", "tile.png").then(image => second = image);
        Assets.load("images", { path: "tile.png", upload: "lock" });
        Assets.update(FRAME);
        await flush();
        check("reuse while loading locks on arrival", second && second.isLocked);

        // Released: unlocked and freed.
        Assets.release(first);
        Assets.release(first);
        Assets.release(second);
        Assets.release(second);
        check("freed and unlocked", first.freed && !first.isLocked && second.freed);

        // A queued image released before it loads is cancelled.
        const queued = Assets.acquire({ images: { late: "late.png" } });
        queued.release();
        Assets.update(FRAME);
        await flush();
        check("queued image cancelled", Assets.stats().entries === 0 || console.log(JSON.stringify(Assets.list())));

        // Idle: the decoder thread closes after imageListIdleTime, and opens again when needed.
        Assets.imageListIdleTime = 1;
        Assets.update(0.5);
        check("open while recently used", Assets.stats().imageListOpen);
        Assets.update(0.6);
        check("closed when idle", !Assets.stats().imageListOpen);
        Assets.load("images", "again.png").then(image => Assets.release(image));
        check("opened again on demand", FakeImageList.made === 2 && Assets.stats().imageListOpen);
        Assets.update(FRAME);
        await flush();
        Assets.update(2);
        check("closed again", !Assets.stats().imageListOpen);
    } finally {
        Assets.imageListIdleTime = 5;
        closeImageList();
        globalThis.ImageList = saved;
    }

    // A kind with its own reuse().
    const reused = [];
    Assets.define("reusable", {
        load(path) { return { path }; },
        free() {},
        reuse(asset, spec) { reused.push(spec.mode); },
    });
    const one = await Assets.load("reusable", { path: "r", mode: "first" });
    await Assets.load("reusable", { path: "r", mode: "second" });
    check("reuse hook", reused.join() === "second");
    Assets.release(one);
    Assets.release(one);
    throws("reuse must be a function", () => Assets.define("x", { load() {}, free() {}, reuse: 1 }), TypeError);
}

async function transitionsTest() {
    Scene.reset();
    Scene.loadingScreen = null;
    Scene.go(P);
    await step();

    // Wipe: a band grows from the right edge, then shrinks ahead of it on the left.
    globalThis.__debugCalls = [];
    Scene.go(Q, { transition: "wipe", duration: 0.2 });
    await step(3);
    const bands = globalThis.__debugCalls.filter(c => c[0] === "rect");
    if (host) check("wipe out: a band from the right edge", bands.length > 0 &&
        bands.every(c => c[1] + c[3] === 640 && c[3] < 640 && c[4] === 448));
    await step(4);
    globalThis.__debugCalls = [];
    await step(3);
    const ins = globalThis.__debugCalls.filter(c => c[0] === "rect");
    if (host) check("wipe in: a band on the left", ins.length > 0 && ins.every(c => c[1] === 0 && c[3] < 640));
    await step(6);
    globalThis.__debugCalls = [];
    Scene.go(P, { transition: "wipe", direction: "down", duration: 0.2 });
    await step(3);
    const vertical = globalThis.__debugCalls.filter(c => c[0] === "rect");
    if (host) check("wipe down: a band from the top", vertical.length > 0 &&
        vertical.every(c => c[2] === 0 && c[3] === 640 && c[4] < 448));
    await step(12);

    // A custom transition sees the amount rise, hold while loading, and fall.
    const seen = [];
    const custom = { draw(amount, info) { seen.push([info.phase, amount]); } };
    class Waits extends Base { static assets = { fake: { w: "wait" } }; }
    Scene.go(Waits, { transition: custom, duration: 0.2 });
    await step(10);
    complete("wait");
    await step(10);
    const phases = [...new Set(seen.map(([phase]) => phase))].join();
    const outs = seen.filter(([phase]) => phase === "out").map(([, a]) => a);
    const outsRise = outs.every((a, i) => i === 0 || a >= outs[i - 1]);
    check("custom transition phases", phases === "out,hold,in" && outsRise &&
        seen.filter(([phase]) => phase === "hold").every(([, a]) => a === 1));
    check("custom transition ends", Scene.current instanceof Waits && !Scene.busy);

    // A transition without a duration uses the default (0.4 s) and ends.
    let ended = false;
    Scene.go(Q, { transition: "fade" }).then(() => ended = true);
    await step(30);
    check("default duration", ended && Scene.current instanceof Q && !Scene.busy);
    ended = false;
    Scene.go(P, { transition: "wipe" }).then(() => ended = true);
    await step(30);
    check("default duration (wipe)", ended && Scene.current instanceof P);

    // Named transitions.
    const named = [];
    Scene.defineTransition("flash", amount => named.push(amount));
    Scene.go(P, { transition: "flash", duration: 0.1 });
    await step(10);
    check("defineTransition", named.length > 0 && Scene.current instanceof P);
    throws("defineTransition name", () => Scene.defineTransition("none", () => {}), TypeError);
    throws("defineTransition draw", () => Scene.defineTransition("x", 1), TypeError);
    throws("unknown transition", () => Scene.go(P, { transition: "spiral" }), TypeError);
    throws("bad direction", () => Scene.go(P, { transition: "wipe", direction: "diagonal" }), TypeError);
    Scene.reset();
}

async function timeouts() {
    Scene.reset();
    // A group that does not load in time fails, naming what was still loading.
    const group = Assets.acquire({ fake: { quick: "sync-quick", slow: "slow-1" } }, { timeout: 0.05 });
    await flush();
    Assets.update(0.03);
    check("not yet", !group.failed);
    Assets.update(0.07);   /* timeouts count the frames' real time */
    let error = null;
    try { await group.ready; } catch (e) { error = e; }
    check("group timeout", group.failed && error && error.name === "TimeoutError" &&
        error.code === "TIMEOUT" && error.pending.length === 1 && error.pending[0].path === "slow-1" &&
        /still loading: fake 'slow-1'/.test(error.message));
    check("pending()", group.pending().length === 1 && group.pending()[0].name === "slow");
    group.release();
    complete("slow-1");
    await flush();
    check("the late load is freed", fake.freed.includes("slow-1"));

    // One asset with a timeout.
    let rejected = null;
    Assets.load("fake", "slow-2", { timeout: 0.05 }).catch(e => rejected = e);
    Assets.update(0.07);   /* timeouts count the frames' real time */
    await flush();
    check("load timeout", rejected && rejected.name === "TimeoutError" && /slow-2/.test(rejected.message));
    complete("slow-2");
    await flush();
    check("its hold released", fake.freed.includes("slow-2"));
    // In time: resolves, and the timeout does not fire later.
    let value = null;
    rejected = null;
    Assets.load("fake", "slow-3", { timeout: 0.05 }).then(v => value = v, e => rejected = e);
    complete("slow-3");
    await flush();
    Assets.update(0.07);   /* timeouts count the frames' real time */
    await flush();
    check("loaded in time", value && value.path === "slow-3" && rejected === null);
    Assets.release(value);
    check("root in options", Assets.load("fake", "sync-o", { root: "dir" }) instanceof Promise &&
        Assets.list().some(entry => entry.path === "dir/sync-o"));
    await flush();
    Assets.release("fake", "sync-o", "dir");
    throws("bad timeout", () => Assets.acquire({}, { timeout: 0 }), RangeError);
    throws("bad options", () => Assets.load("fake", "a", 3), TypeError);

    // A scene that does not load in time fails like a missing file, and names what hung.
    class Forever extends Base { static assets = { fake: { hang: "forever" } }; }
    const errors = [];
    Scene.onError = e => errors.push(e);
    Scene.go(Forever, { timeout: 0.1 });
    await step(10);
    check("scene timeout", errors.length === 1 && errors[0].name === "TimeoutError" &&
        errors[0].message.startsWith("Scene.go(Forever): not loaded after 0.1 s; still loading: fake 'forever'") &&
        errors[0].code === "TIMEOUT" && Scene.current === null && !Scene.busy);
    // The global default, and the slow-load warning.
    const logged = [];
    const log0 = console.log;
    console.log = (...args) => logged.push(args.join(" "));
    try {
        Scene.slowLoadWarning = 0.05;
        Scene.loadTimeout = 0.2;
        Scene.go(Forever);
        await step(20);
    } finally {
        console.log = log0;
        Scene.slowLoadWarning = 10;
        Scene.loadTimeout = 0;
    }
    check("slow-load warning once, naming the asset", logged.length === 1 &&
        /Scene\.go\(Forever\): still loading after 0\.05 s: fake 'forever'/.test(logged[0]));
    check("global loadTimeout", errors.length === 2 && errors[1].name === "TimeoutError");
    throws("bad loadTimeout", () => { Scene.loadTimeout = -1; }, RangeError);
    throws("bad request timeout", () => Scene.go(P, { timeout: 0 }), RangeError);
    Scene.onError = null;
    complete("forever");
    await flush();
    Scene.reset();
}

async function main() {
    await assets();
    await builtinKinds();
    await scenes();
    await robustness();
    await lifetimes();
    await deferredFrees();
    await sheetOptions();
    runChecks();
    await fileReads();
    await imageLists();
    await transitionsTest();
    await timeouts();
    await realTime();
    // A scene and assets left at the end: the runtime must still free everything.
    Scene.go(P);
    Scene.update(FRAME);
    globalThis.__kept = Assets.acquire({ fake: { kept: "sync-kept" } });
    console.log(`Result: ${passed} passed, ${failed} failed` + (host ? "" : " (host-only checks skipped)"));
    if (!failed) console.log("Scene module test passed");
}

main().catch(error => {
    console.log("[FAIL] " + error + "\n" + (error && error.stack));
});
