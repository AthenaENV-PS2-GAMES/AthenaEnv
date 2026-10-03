/*
 * Sprite module: sheets (grids, frames, Aseprite/TexturePacker JSON), clips,
 * playback and events of Sprite.Instance, its Loop system, drawing, and
 * Sprite.Animator over TileMap sprite buffers. Runs on PCSX2/PS2 and on the
 * host (tests/js/run.sh), where stand-ins of Image and TileMap record draws
 * (__spriteDraws); those checks are skipped on the console. Objects are left
 * alive at the end on purpose: teardown must free them without leaks.
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

const near = (a, b, eps = 1e-3) => Math.abs(a - b) <= eps;
const host = typeof globalThis.__spriteDraws === "function";
const { Sheet, Instance, Animator } = Sprite;

function openFirst(paths, open) {
    for (const path of paths) {
        try { return open(path); } catch (error) { /* next */ }
    }
    throw new Error("not found: " + paths.join(", "));
}

const TEXTURE = openFirst(["tests/texture.png", "texture.png", "bin/tests/texture.png"],
    path => new Image(path));
const JSON_PATHS = ["tests/sprite_sheet.json", "sprite_sheet.json", "bin/tests/sprite_sheet.json"];

/* Runs `count` whole frames of the Loop, then stops in the next update. */
function frames(count) {
    return new Promise(resolve => {
        let frame = 0;
        Loop.run({
            update() {
                if (frame === count) {
                    Loop.stop();
                    resolve();
                }
            },
            draw() { frame++; },
        });
    });
}

function sheets() {
    check("Sprite global", typeof Sprite === "object" && typeof Sheet === "function" &&
        typeof Instance === "function" && typeof Animator === "function");

    // 256 x 96 texture, 32 x 32 cells: 8 x 3.
    const grid = Sheet.fromGrid(TEXTURE, { frameWidth: 32, frameHeight: 32 });
    check("grid frames", grid.frameCount === 24);
    check("grid image", grid.image === TEXTURE);
    const cell = grid.getFrame(9);
    check("grid cell 9", cell.x === 32 && cell.y === 32 && cell.w === 32 && cell.name === null);
    const spaced = Sheet.fromGrid(null, { frameWidth: 30, frameHeight: 30, margin: 1, spacing: 2,
        textureWidth: 128, textureHeight: 64, first: 1, count: 3 });
    check("grid margin, spacing, first, count", spaced.frameCount === 3 &&
        spaced.getFrame(0).x === 33 && spaced.getFrame(0).y === 1 && spaced.image === null);
    throws("grid without frame size", () => Sheet.fromGrid(TEXTURE, {}), TypeError);
    throws("grid larger than the texture", () => Sheet.fromGrid(TEXTURE, { frameWidth: 512, frameHeight: 8 }),
        RangeError);
    throws("grid without texture size", () => Sheet.fromGrid(null, { frameWidth: 8, frameHeight: 8 }),
        RangeError);
    throws("grid image type", () => Sheet.fromGrid({}, { frameWidth: 8, frameHeight: 8 }), TypeError);

    // Clips.
    grid.addClip("run", "4-7")
        .addClip("list", { frames: [0, 2, 1], fps: 4 })
        .addClip("slow", { frames: "0-1", durations: [100, 300], mode: "once" })
        .addClip("pp", { frames: "0-3", fps: 10, mode: "pingpong", loops: 2 })
        .addClip("back", { frames: "0-2", fps: 10, reverse: true });
    check("clip names", grid.clipNames.join() === "run,list,slow,pp,back");
    check("hasClip", grid.hasClip("run") && !grid.hasClip("nope") && !grid.hasClip(3));
    let info = grid.getClip("run");
    check("default 12 fps", info.frames.join() === "4,5,6,7" && near(info.durations[0], 1000 / 12) &&
        info.mode === "loop" && info.loops === 0);
    info = grid.getClip("slow");
    check("durations and once", info.durations.map(Math.round).join() === "100,300" && info.mode === "once" &&
        info.loops === 1 && near(info.length, 400));
    check("pingpong loops", grid.getClip("pp").mode === "pingpong" && grid.getClip("pp").loops === 2);
    check("reverse", grid.getClip("back").frames.join() === "2,1,0");
    check("getClip unknown", grid.getClip("nope") === null);
    grid.addClip("run", { frames: "8-9", fps: 20 });
    check("replace clip", grid.getClip("run").frames.join() === "8,9" && grid.clipNames.length === 5);
    throws("frame out of range", () => grid.addClip("x", "0-24"), RangeError);
    throws("bad range", () => grid.addClip("x", "1-"), SyntaxError);
    throws("bad mode", () => grid.addClip("x", { frames: "0", mode: "bounce" }), TypeError);
    throws("durations length", () => grid.addClip("x", { frames: "0-1", durations: [100] }), RangeError);
    throws("zero fps", () => grid.addClip("x", { frames: "0", fps: 0 }), RangeError);
    throws("unknown frame name", () => grid.addClip("x", ["a.png"]), RangeError);
    throws("empty clip name", () => grid.addClip("", "0"), TypeError);
    check("failed clips add nothing", !grid.hasClip("x"));

    // Frames one by one, with trim and names.
    const manual = new Sheet(TEXTURE, {
        frames: [
            { x: 0, y: 0, w: 10, h: 12, offsetX: 3, offsetY: 2, sourceWidth: 16, sourceHeight: 16,
                name: "walk_0", duration: 50 },
            { x: 16, y: 0, w: 16, h: 16, name: "walk_1", duration: 70 },
        ],
        clips: { walk: ["walk_0", "walk_1"], fast: { frames: [0, 1], fps: 30 } },
    });
    check("manual frames", manual.frameCount === 2 && manual.findFrame("walk_1") === 1 &&
        manual.findFrame("nope") === -1);
    const trimmed = manual.getFrame("walk_0");
    check("trim", trimmed.offsetX === 3 && trimmed.sourceWidth === 16 && trimmed.name === "walk_0" &&
        near(trimmed.duration, 50));
    check("clip from frame durations", manual.getClip("walk").durations.map(Math.round).join() === "50,70");
    check("fps overrides frame durations", near(manual.getClip("fast").durations[0], 1000 / 30));
    check("addFrame", manual.addFrame({ x: 32, y: 0, w: 8, h: 8 }) === 2 && manual.frameCount === 3);
    throws("trim past the source", () => manual.addFrame({ x: 0, y: 0, w: 10, h: 10, offsetX: 8,
        sourceWidth: 16 }), RangeError);
    throws("duplicate frame name", () => manual.addFrame({ x: 0, y: 0, w: 8, h: 8, name: "walk_0" }), TypeError);
    throws("frame without size", () => manual.addFrame({ x: 0, y: 0 }), TypeError);
    return { grid, manual };
}

function json() {
    // Aseprite, already parsed: hash frames, tags with direction and repeat.
    const aseprite = {
        frames: {
            "a 0": { frame: { x: 0, y: 0, w: 16, h: 16 }, sourceSize: { w: 16, h: 16 },
                spriteSourceSize: { x: 0, y: 0, w: 16, h: 16 }, duration: 100 },
            "a 1": { frame: { x: 16, y: 0, w: 12, h: 14 }, sourceSize: { w: 16, h: 16 },
                spriteSourceSize: { x: 2, y: 1, w: 12, h: 14 }, duration: 200 },
            "a 2": { frame: { x: 32, y: 0, w: 16, h: 16 }, duration: 100 },
        },
        meta: {
            image: "atlas.png",
            frameTags: [
                { name: "walk", from: 0, to: 2, direction: "forward" },
                { name: "swing", from: 0, to: 2, direction: "pingpong_reverse", repeat: "3" },
            ],
        },
    };
    const sheet = Sheet.fromJSON(aseprite, { image: TEXTURE });
    check("aseprite frames", sheet.frameCount === 3 && sheet.findFrame("a 1") === 1);
    const f1 = sheet.getFrame(1);
    check("aseprite trim", f1.offsetX === 2 && f1.offsetY === 1 && f1.sourceWidth === 16 && near(f1.duration, 200));
    let clip = sheet.getClip("walk");
    check("aseprite tag", clip.frames.join() === "0,1,2" && clip.durations.map(Math.round).join() === "100,200,100");
    clip = sheet.getClip("swing");
    check("aseprite pingpong_reverse and repeat", clip.frames.join() === "2,1,0" && clip.mode === "pingpong" &&
        clip.loops === 3);

    // TexturePacker JSON Array with Pixi-style animations; user clips on top.
    const packer = {
        frames: [
            { filename: "coin_0.png", frame: { x: 0, y: 32, w: 16, h: 16 } },
            { filename: "coin_1.png", frame: { x: 16, y: 32, w: 16, h: 16 } },
        ],
        animations: { spin: ["coin_0.png", "coin_1.png"] },
        meta: { image: "coins.png" },
    };
    const coins = Sheet.fromJSON(packer, { image: null, fps: 8, clips: { flip: { frames: [1, 0], fps: 2 } } });
    check("packer frames", coins.frameCount === 2 && coins.getFrame("coin_1.png").x === 16 && coins.image === null);
    check("packer animations at the default fps", near(coins.getClip("spin").durations[0], 125));
    check("user clips", near(coins.getClip("flip").durations[0], 500));

    check("rotated frames", Sheet.fromJSON({ frames: { r: { frame: { x: 0, y: 0, w: 8, h: 8 },
        rotated: true } } }, { image: null }).getFrame("r").rotated === true);
    throws("no frames", () => Sheet.fromJSON({ meta: {} }, { image: null }), TypeError);
    throws("missing file", () => Sheet.fromJSON("tests/no_such_sheet.json"), ReferenceError);
    throws("no meta.image", () => Sheet.fromJSON({ frames: {} }), TypeError);

    // From a file: the image comes from meta.image, next to the JSON.
    let jsonPath = null;
    const file = openFirst(JSON_PATHS, path => {
        const loaded = Sheet.fromJSON(path);
        jsonPath = path;
        return loaded;
    });
    check("file frames", file.frameCount === 4 && file.image instanceof Image);
    if (host)
        check("image path next to the file",
            file.image.path === jsonPath.replace(/sprite_sheet\.json$/, "texture.png"));
    check("file tags", file.clipNames.join() === "idle,bounce,back" &&
        file.getClip("bounce").loops === 2 && file.getClip("back").frames.join() === "3,2,1");
    return file;
}

function playback({ grid }) {
    const log = [];
    const record = function (clip, position, frame) {
        log.push(`${this === hero ? "" : "?"}${clip}:${position}:${frame}`);
    };
    const hero = new Instance(grid, { autoUpdate: false });
    check("still frame 0", hero.frame === 0 && hero.clip === null && !hero.playing);
    throws("play unknown clip", () => hero.play("nope"), RangeError);
    throws("play without a name", () => hero.play(3), TypeError);

    grid.addClip("walk", { frames: "4-7", fps: 10 });
    hero.play("walk");
    check("playing", hero.playing && hero.clip === "walk" && hero.frame === 4 && hero.position === 0);
    hero.on("frame", record);
    hero.update(0.05);
    check("first frame reported", log.join() === "walk:0:4");
    hero.update(0.1);
    check("next frame", hero.frame === 5 && log.join() === "walk:0:4,walk:1:5");

    // play() of the clip already playing does nothing.
    hero.play("walk");
    check("play again is a no-op", hero.position === 1);
    hero.play("walk", { restart: true });
    check("restart", hero.position === 0);
    hero.play("walk", { position: 3 });
    check("play from a position", hero.position === 3 && hero.frame === 7);

    // Loop and frame:N events, off().
    hero.off();
    log.length = 0;
    hero.on("loop", function (clip) { log.push("loop:" + clip); });
    hero.on("frame:0", function () { log.push("f0"); });
    hero.update(0.1);
    check("loop then frame 0", log.join() === "loop:walk,f0" && hero.cycles === 1);
    hero.off("frame:0");
    log.length = 0;
    hero.update(0.45);
    check("off(event)", log.join() === "loop:walk");

    // Once: the end fires once.
    let ends = 0;
    grid.addClip("die", { frames: "0-2", fps: 10, mode: "once" });
    hero.off().once("end", () => ends++).on("end", () => ends += 10);
    hero.play("die");
    hero.update(0.15);
    check("progress", near(hero.progress, 0.5));
    hero.update(1);
    check("ends once", ends === 11 && hero.finished && !hero.playing && hero.frame === 2 && hero.progress === 1);
    hero.update(1);
    hero.update(1);
    check("no repeated end", ends === 11);
    hero.position = 0;
    check("seek replays a finished clip", hero.playing && !hero.finished);
    hero.update(0.5);
    check("once listener removed", ends === 21);

    // Pingpong with loops.
    const seen = [];
    hero.off().on("frame", (clip, position, frame) => seen.push(frame));
    hero.play("pp");
    hero.update(10);
    check("pingpong twice", seen.join() === "0,1,2,3,2,1,0,1,2,3,2,1,0" && hero.finished);

    // Pause, resume, stop, speed.
    hero.off();
    hero.play("walk", { restart: true });
    hero.pause();
    hero.update(1);
    check("paused", hero.paused && !hero.playing && hero.position === 0);
    hero.resume();
    hero.speed = 2;
    hero.update(0.05);
    check("resume and speed", hero.playing && !hero.paused && hero.position === 1);
    hero.stop();
    check("stop goes back to the first frame", !hero.playing && hero.position === 0 && hero.clip === "walk");
    hero.speed = 1;
    throws("negative speed", () => { hero.speed = -1; }, RangeError);
    check("a rejected value changes nothing", hero.speed === 1);

    // Still frames.
    hero.frame = 9;
    check("frame setter", hero.frame === 9 && hero.clip === null && !hero.playing);
    throws("frame out of range", () => { hero.frame = 24; }, RangeError);
    throws("position without a clip", () => { hero.position = 0; }, TypeError);
    throws("unknown event", () => hero.on("finish", () => {}), TypeError);
    throws("listener must be a function", () => hero.on("end", 1), TypeError);

    // Listeners that remove themselves or add others while dispatching.
    let count = 0;
    const self = () => { count++; hero.off("frame", self); hero.on("frame", () => count += 100); };
    hero.off().on("frame", self);
    hero.play("walk");
    hero.update(0.25);
    check("listeners changed during dispatch", count === 1 + 200);
    hero.off();
    return hero;
}

function drawing({ grid, manual }) {
    const s = new Instance(manual, { frame: "walk_0", origin: [0.5, 1], scale: 2 });
    check("options", s.originX === 0.5 && s.originY === 1 && s.scaleX === 2 && s.scaleY === 2 &&
        s.width === 32 && s.height === 32);
    // walk_0: 10 x 12 at (3, 2) of a 16 x 16 frame.
    let b = s.getBounds(100, 50);
    check("bounds with trim, origin and scale", b.x === 100 + (3 - 8) * 2 && b.y === 50 + (2 - 16) * 2 &&
        b.w === 20 && b.h === 24);
    s.flipX = true;
    b = s.getBounds(100, 50);
    check("flip mirrors the trim", b.x === 100 + (16 - 3 - 10 - 8) * 2);
    s.setScale(1).setOrigin(0);
    check("setScale, setOrigin", s.scaleX === 1 && s.originY === 0);
    throws("negative scale", () => s.setScale(-1), RangeError);
    throws("NaN origin", () => { s.originX = NaN; }, RangeError);
    throws("draw coordinates", () => s.draw(Infinity, 0), RangeError);

    if (host) {
        __spriteDraws();
        s.color = 0x80808040;   // red 64 in the low byte
        s.draw(10, 20);
        const d = __spriteDraws();
        check("draw", d.count === 1 && d.last[0] === 10 + 16 - 3 - 10 && d.last[1] === 22 &&
            d.last[2] === 10 && d.last[3] === 12);
        check("draw flipped uv", d.last[4] === 10 && d.last[6] === 0 && d.last[5] === 0 && d.last[7] === 12);
        check("draw color", d.last[9] === 64);
        s.rotation = Math.PI / 2;
        s.flipX = false;
        s.draw(0, 0);
        check("rotation passed on", near(__spriteDraws().last[8], Math.PI / 2));

        const missing = new Instance(Sheet.fromGrid(new Image("missing.png"), { frameWidth: 8,
            frameHeight: 8, textureWidth: 16, textureHeight: 16 }));
        missing.draw(0, 0);
        check("an image not loaded draws nothing", __spriteDraws().count === 0);
    }
    const noImage = new Instance(Sheet.fromGrid(null, { frameWidth: 8, frameHeight: 8,
        textureWidth: 16, textureHeight: 16 }));
    throws("draw without an image", () => noImage.draw(0, 0), TypeError);
    throws("sheet without frames", () => new Instance(new Sheet(null)), RangeError);
    throws("instance of a non-sheet", () => new Instance({}), TypeError);
    throws("bad options", () => new Instance(grid, 5), TypeError);
}

async function loopSystem({ grid }) {
    grid.addClip("tick", { frames: "0-3", fps: 10 });
    const a = new Instance(grid, { clip: "tick" });
    const manual = new Instance(grid, { clip: "tick", autoUpdate: false });
    let frameEvents = 0;
    a.on("frame", () => frameEvents++);
    check("stats", Sprite.getStats().playing >= 1);

    await frames(15);   // 0.25 s
    check("advanced by the Loop", a.position === 2 && frameEvents === 3);
    check("autoUpdate false is left alone", manual.position === 0);

    Loop.setTimeScale(0);
    await frames(10);
    check("time scale 0 pauses", a.position === 2);
    Loop.setTimeScale(1);

    // An exception in a listener reaches the caller (inside the Loop, it stops the Loop).
    a.on("frame", () => { throw new Error("listener failed"); });
    const error = throws("listener exception", () => Sprite.update(0.1), Error);
    check("listener exception message", error && /listener failed/.test(String(error)));
    a.off();

    // Sprite.update() outside the Loop.
    const before = a.position;
    Sprite.update(0.1);
    check("Sprite.update", a.position === (before + 1) % 4);
    throws("Sprite.update dt", () => Sprite.update(-1), RangeError);
    return a;
}

function animator({ grid }) {
    const count = 4;
    const buffer = TileMap.SpriteBuffer.create(count);
    const instance = host ? new TileMap.Instance({ spriteBuffer: buffer }) :
        new TileMap.Instance({
            descriptor: new TileMap.Descriptor({ textures: [TEXTURE], materials: [{ endOffset: count - 1 }] }),
            spriteBuffer: buffer,
        });
    const floats = new Float32Array(buffer);
    const stride = TileMap.layout.stride / 4;
    const u1 = i => floats[i * stride + TileMap.layout.offsets.u1 / 4];
    const u2 = i => floats[i * stride + TileMap.layout.offsets.u2 / 4];
    const v1 = i => floats[i * stride + TileMap.layout.offsets.v1 / 4];

    grid.addClip("spin", { frames: "8-11", fps: 10 });
    floats[TileMap.layout.offsets.x / 4] = 123;
    const anim = Animator.bind(instance, grid, "spin", { first: 1, count: 2 });
    check("bound", anim.bound && anim.count === 2 && anim.first === 1 && anim.instance === instance &&
        anim.sheet === grid);
    // Frame 8 is cell (0, 1): x 0..32, y 32.
    check("first frame written at once", u1(1) === 0 && u2(1) === 32 && v1(1) === 32 && u1(2) === 0);
    check("other sprites untouched", u2(0) === 0 && u2(3) === 0 && floats[0] === 123);
    check("frameAt", anim.frameAt(0) === 8);

    Sprite.update(0.1);
    check("advanced", anim.frameAt(0) === 9 && u1(1) === 32);
    anim.setFlip(true, false, { first: 1, count: 1 });
    Sprite.update(0);
    check("flip one sprite", u1(2) === 64 && u2(2) === 32 && u1(1) === 32);
    anim.pause();
    Sprite.update(1);
    check("paused", anim.paused && anim.frameAt(0) === 9);
    anim.resume();
    anim.speed = 2;
    Sprite.update(0.05);
    check("speed", anim.frameAt(0) === 10);
    anim.play("run", { first: 0, count: 1 });
    Sprite.update(0);
    check("play another clip on part", anim.frameAt(0) === 8 && anim.frameAt(1) === 10);
    check("stats count sprites", Sprite.getStats().animatedSprites >= 2);

    const random = Animator.bind(instance, grid, "spin", { randomStart: true, seed: 7 });
    check("random start in range", random.count === count &&
        [0, 1, 2, 3].every(i => random.frameAt(i) >= 8 && random.frameAt(i) <= 11));
    random.unbind();
    check("unbind", !random.bound);
    random.unbind();

    throws("bind a non-instance", () => Animator.bind({}, grid, "spin"), TypeError);
    throws("bind a non-sheet", () => Animator.bind(instance, {}, "spin"), TypeError);
    throws("bind an unknown clip", () => Animator.bind(instance, grid, "nope"), RangeError);
    throws("bind past the buffer", () => Animator.bind(instance, grid, "spin", { first: 2, count: 3 }), RangeError);
    throws("new Animator", () => new Animator(), TypeError);

    // A smaller buffer: sprites past it are skipped, not written.
    if (host) {
        instance.replaceSpriteBuffer(TileMap.SpriteBuffer.create(1));
        Sprite.update(0.1);
        check("smaller buffer", true);
        instance.replaceSpriteBuffer(buffer);
    }
    return anim;
}

function garbage({ grid }) {
    // Sprites with listeners that capture themselves, dropped: collected.
    for (let i = 0; i < 50; i++) {
        const s = new Instance(grid, { clip: "tick" });
        s.on("end", () => s.stop());
    }
    std.gc();
    Sprite.update(0.01);
    check("dropped sprites leave the list", Sprite.getStats().playing < 50);
}

function chaining() {
    const sheet = Sheet.fromGrid(TEXTURE, { frameWidth: 32, frameHeight: 32, clips: {
        attack: { frames: "0-1", fps: 10, mode: "once", next: "idle" },
        idle: { frames: "4-5", fps: 10 },
    } });
    check("next in getClip", sheet.getClip("attack").next === "idle" && sheet.getClip("idle").next === null);
    throws("next must be a name", () => sheet.addClip("x", { frames: "0", next: 3 }), TypeError);
    const s = new Instance(sheet, { autoUpdate: false });
    const log = [];
    s.on("end:attack", (clip) => log.push("end " + clip));
    s.on("idle:0", () => log.push("idle starts"));
    s.on("frame:1", (clip) => log.push("f1 " + clip));
    s.play("attack");
    s.update(0.25);
    check("next plays after the end", s.clip === "idle" && s.playing && s.frame === 4);
    check("filtered events", log.join() === "f1 attack,end attack,idle starts");
    throws("filter with an unknown clip", () => s.on("jump:2", () => {}), RangeError);
    throws("end filter with an unknown clip", () => s.on("end:jump", () => {}), RangeError);
    throws("bad position", () => s.on("idle:x", () => {}), TypeError);
    s.off("end:attack");
    s.off("idle:0");
    s.off("frame:1");

    // stop() pauses on the first frame; resume() plays from there.
    s.play("idle", { restart: true });
    s.update(0.15);
    s.stop();
    check("stop pauses at the start", !s.playing && s.paused && s.position === 0);
    s.resume();
    s.update(0.15);
    check("resume after stop", s.playing && s.position === 1);
    return sheet;
}

async function promises(sheet) {
    const s = new Instance(sheet, { autoUpdate: false });
    let result = null;
    s.playAsync("attack").then(value => result = value);
    s.update(0.25);
    await null;
    await null;
    check("playAsync resolves true at the end", result === true && s.clip === "idle");

    result = null;
    s.playAsync("attack").then(value => result = value);
    s.play("idle", { restart: true });
    await null;
    await null;
    check("playAsync resolves false when replaced", result === false);

    result = null;
    s.playAsync("attack").then(value => result = value);
    s.stop();
    await null;
    await null;
    check("stop settles false", result === false);

    // An instance awaited without being kept still answers (it pins itself).
    let dropped = null;
    (() => {
        const temp = new Instance(sheet, { autoUpdate: true });
        temp.playAsync("attack").then(value => dropped = value);
    })();
    std.gc();
    Sprite.update(0.3);
    await null;
    await null;
    check("an awaited instance stays alive", dropped === true);

    // One left pending at the end of the script: cleanup drops it without leaking.
    globalThis.__pendingSprite = new Instance(sheet, { autoUpdate: false });
    globalThis.__pendingSprite.playAsync("idle");
}

function batches({ grid }) {
    const a = new Instance(grid, { frame: 1 });
    const b = new Instance(grid, { frame: 2, flipX: true, color: 0x80808020 });
    const c = new Instance(grid, { frame: 3, x: 5, y: 6 });
    check("x, y options", c.x === 5 && c.y === 6);
    if (host) {
        __spriteDraws();
        Sprite.drawAll([a, b, c], new Float32Array([10, 20, 30, 40, 50, 60]));
        let d = __spriteDraws();
        check("one list for one texture", d.lists === 1 && d.listSprites === 3 && d.count === 0);
        check("last sprite of the list", d.last[0] === 50 && d.last[1] === 60 && d.last[4] === 96);
        check("positions stored", a.x === 10 && b.y === 40 && c.x === 50);
        check("stats drawn", Sprite.getStats().drawn === 3);

        b.rotation = 0.5;
        Sprite.drawAll([a, b, c]);
        d = __spriteDraws();
        check("a rotated instance goes alone, in order", d.lists === 2 && d.listSprites === 2 && d.count === 1);
        b.rotation = 0;

        Sprite.drawAll([a, b], [1, 2, 3, 4]);
        check("array positions", __spriteDraws().listSprites === 2 && b.x === 3);

        c.draw();
        d = __spriteDraws();
        check("draw() at its own x, y", d.count === 1 && d.last[0] === 50 && d.last[1] === 60);

        // Many sprites: chunks of 128.
        const many = [];
        for (let i = 0; i < 300; i++) many.push(a);
        Sprite.drawAll(many, new Float32Array(600));
        d = __spriteDraws();
        check("chunks of 128", d.lists === 3 && d.listSprites === 300);

        // Under a camera looking elsewhere, sprites are culled.
        const cam = new Camera2D.Camera({ x: 100000, y: 100000 });
        cam.draw(() => Sprite.drawAll([a, c]));
        check("culled under a camera", Sprite.getStats().culled === 2 && __spriteDraws().listSprites === 0);

        // The inset shrinks the texture rectangle, not the sprite.
        grid.inset = 0.5;
        Sprite.drawAll([c]);
        d = __spriteDraws();
        check("inset", d.last[4] === 96.5 && d.last[6] === 127.5 && d.last[2] === 32);
        grid.inset = 0;
    }
    throws("drawAll of a non-array", () => Sprite.drawAll(a), TypeError);
    throws("drawAll of a non-instance", () => Sprite.drawAll([a, {}]), TypeError);
    throws("too few positions", () => Sprite.drawAll([a, b], [1, 2, 3]), RangeError);
    throws("NaN position", () => Sprite.drawAll([a], new Float32Array([NaN, 0])), RangeError);
    throws("bad positions", () => Sprite.drawAll([a], "x"), TypeError);
    throws("inset range", () => { grid.inset = -1; }, RangeError);
    check("inset unchanged", grid.inset === 0);
    Sprite.drawAll([]);
}

function slices() {
    const sheet = Sheet.fromGrid(TEXTURE, { frameWidth: 32, frameHeight: 32 });
    sheet.setSlice("hit", { x: 4, y: 8, w: 10, h: 12 }, "2-3");
    sheet.setSlice("feet", { x: 0, y: 28, w: 32, h: 4 });
    check("sliceNames", sheet.sliceNames.join() === "hit,feet");
    const r = sheet.getSlice("hit", 2);
    check("getSlice", r && r.x === 4 && r.w === 10 && sheet.getSlice("hit", 1) === null &&
        sheet.getSlice("nope", 0) === null);
    sheet.setSlice("hit", null, 3);
    check("remove from one frame", sheet.getSlice("hit", 3) === null);

    const s = new Instance(sheet, { frame: 2, origin: [0.5, 1], scale: 2, x: 100, y: 200 });
    let w = s.getSlice("hit");
    check("world slice", w && w.x === 100 + (4 - 16) * 2 && w.y === 200 + (8 - 32) * 2 && w.w === 20);
    s.flipX = true;
    w = s.getSlice("hit", 0, 0);
    check("flipped slice", w.x === (32 - 4 - 10 - 16) * 2);
    s.frame = 0;
    check("frame without the slice", s.getSlice("hit") === null);
    throws("slice name", () => s.getSlice(3), TypeError);
    throws("setSlice rect", () => sheet.setSlice("x", { x: 0 }), TypeError);

    // Aseprite slices: a key holds until the next key.
    const data = {
        frames: [0, 1, 2, 3].map(i => ({ filename: "f" + i, frame: { x: i * 16, y: 0, w: 16, h: 16 }, duration: 100 })),
        meta: { slices: [{ name: "hurt", keys: [
            { frame: 1, bounds: { x: 1, y: 2, w: 3, h: 4 } },
            { frame: 3, bounds: { x: 5, y: 6, w: 7, h: 8 } },
        ] }] },
    };
    const ase = Sheet.fromJSON(data, { image: TEXTURE });
    check("aseprite slices", ase.getSlice("hurt", 0) === null && ase.getSlice("hurt", 1).x === 1 &&
        ase.getSlice("hurt", 2).w === 3 && ase.getSlice("hurt", 3).x === 5);
}

async function realTime({ grid }) {
    grid.addClip("rt", { frames: "0-3", fps: 10 });
    const ui = new Instance(grid, { clip: "rt", realTime: true });
    const game = new Instance(grid, { clip: "rt" });
    check("realTime option", ui.realTime && !game.realTime);
    Loop.setTimeScale(0);
    await frames(15);
    Loop.setTimeScale(1);
    check("real time plays while paused", ui.position === 2 && game.position === 0);
    ui.stop();
    game.stop();
}

function safety({ grid }) {
    // A listener advancing sprites from inside its own event: bounded.
    const s = new Instance(grid, { clip: "tick", autoUpdate: false });
    s.on("frame", () => s.update(0.1));
    const error = throws("nested advances", () => s.update(0.1), RangeError);
    check("nested advance message", error && /levels deep/.test(String(error)));
    s.off();

    // A seed gives each animator the same start, whatever else was bound.
    const buffer = TileMap.SpriteBuffer.create(8);
    const instance = host ? new TileMap.Instance({ spriteBuffer: buffer }) :
        new TileMap.Instance({
            descriptor: new TileMap.Descriptor({ textures: [TEXTURE], materials: [{ endOffset: 7 }] }),
            spriteBuffer: buffer,
        });
    const frames = anim => [0, 1, 2, 3, 4, 5, 6, 7].map(i => anim.frameAt(i)).join();
    const a = Animator.bind(instance, grid, "spin", { randomStart: true, seed: 42 });
    const first = frames(a);
    a.unbind();
    Animator.bind(instance, grid, "spin", { randomStart: true }).unbind();
    const b = Animator.bind(instance, grid, "spin", { randomStart: true, seed: 42 });
    check("seeded start repeats", frames(b) === first);
    b.unbind();
}

function turnedFrames() {
    // TexturePacker: "rotated" frames are stored turned 90 degrees clockwise.
    const data = {
        frames: {
            "a.png": { frame: { x: 0, y: 0, w: 16, h: 16 } },
            "b.png": { frame: { x: 32, y: 0, w: 10, h: 20 }, rotated: true },
        },
        animations: { both: ["a.png", "b.png"] },
    };
    const sheet = Sheet.fromJSON(data, { image: TEXTURE });
    check("rotated frame kept", sheet.getFrame("b.png").rotated && !sheet.getFrame("a.png").rotated);
    const s = new Instance(sheet, { frame: "b.png", x: 5, y: 7 });
    if (host) {
        __spriteDraws();
        s.draw();
        let d = __spriteDraws();
        check("a turned frame draws as a quad", d.quads === 1 && d.count === 0);
        // Shown top-left is stored at (x + h, y); bottom-left at (x, y).
        check("turned texels", d.lastQuad[8] === 52 && d.lastQuad[12] === 0 &&
            d.lastQuad[10] === 32 && d.lastQuad[0] === 5 && d.lastQuad[4] === 7);
        const a = new Instance(sheet, { frame: "a.png" });
        Sprite.drawAll([a, s, a]);
        d = __spriteDraws();
        check("drawAll keeps the order around turned frames", d.quads === 1 && d.lists === 2);
    }
    const buffer = TileMap.SpriteBuffer.create(2);
    const instance = host ? new TileMap.Instance({ spriteBuffer: buffer }) :
        new TileMap.Instance({
            descriptor: new TileMap.Descriptor({ textures: [TEXTURE], materials: [{ endOffset: 1 }] }),
            spriteBuffer: buffer,
        });
    throws("an animator refuses turned frames", () => Animator.bind(instance, sheet, "both"), TypeError);
}

function gridLines({ grid }) {
    // 256 x 96 in 32 x 32 cells: 8 columns, 3 rows.
    grid.addClip("row1", { frames: { row: 1 } })
        .addClip("back", { frames: { row: 1, from: 5, to: 2 } })
        .addClip("col2", { frames: { column: 2 } })
        .addClip("short", { row: 2, from: 0, to: 1, fps: 8 });
    check("row", grid.getClip("row1").frames.join() === "8,9,10,11,12,13,14,15");
    check("row backwards", grid.getClip("back").frames.join() === "13,12,11,10");
    check("column", grid.getClip("col2").frames.join() === "2,10,18");
    check("row as the definition", grid.getClip("short").frames.join() === "16,17" &&
        near(grid.getClip("short").durations[0], 125));
    throws("row out of range", () => grid.addClip("x", { frames: { row: 3 } }), RangeError);
    throws("no frames", () => grid.addClip("x", { fps: 3 }), TypeError);
    const manual = new Sheet(TEXTURE, { frames: [{ x: 0, y: 0, w: 8, h: 8 }] });
    throws("rows need a grid", () => manual.addClip("x", { row: 0 }), TypeError);
    const skipped = Sheet.fromGrid(TEXTURE, { frameWidth: 32, frameHeight: 32, first: 2 });
    throws("cells before first", () => skipped.addClip("x", { row: 0 }), RangeError);
    skipped.addClip("tail", { row: 0, from: 2, to: 3 });
    check("cells after first", skipped.getClip("tail").frames.join() === "0,1");
}

async function loading() {
    let sheet = null, error = null;
    for (const path of JSON_PATHS) {
        try { sheet = await Sheet.fromJSONAsync(path, { clips: { all: "0-3" } }); break; }
        catch (e) { error = e; }
    }
    check("fromJSONAsync", sheet instanceof Sheet && sheet.frameCount === 4 &&
        sheet.hasClip("bounce") && sheet.hasClip("all") && sheet.image instanceof Image);
    if (host && sheet)
        check("texture decoded next to the file", /texture\.png$/.test(sheet.image.path));

    const withImage = await Sheet.fromJSONAsync(
        JSON_PATHS.find(p => { try { Sheet.fromJSON(p, { image: null }); return true; } catch (e) { return false; } }),
        { image: TEXTURE });
    check("fromJSONAsync with an image", withImage.image === TEXTURE);

    let grid = null;
    for (const path of ["tests/texture.png", "texture.png", "bin/tests/texture.png"]) {
        try { grid = await Sheet.fromGridAsync(path, { frameWidth: 32, frameHeight: 32 }); break; }
        catch (e) { error = e; }
    }
    check("fromGridAsync", grid instanceof Sheet && grid.frameCount === 24);

    error = null;
    try { await Sheet.fromJSONAsync("tests/no_such_sheet.json"); } catch (e) { error = e; }
    check("a missing file rejects", error instanceof Error && /cannot open/.test(error.message));
    error = null;
    try { await Sheet.fromGridAsync("tests/no_such_image.png", { frameWidth: 8, frameHeight: 8 }); }
    catch (e) { error = e; }
    check("a missing texture rejects", error instanceof Error && /texture/.test(error.message));
    throws("fromGridAsync options", () => Sheet.fromGridAsync("a.png"), TypeError);
    throws("fromJSONAsync path", () => Sheet.fromJSONAsync({}), TypeError);
}

function debugOverlay({ grid }) {
    const a = new Instance(grid, { frame: 1, debug: true, x: 10, y: 10 });
    const b = new Instance(grid, { frame: 2, x: 50, y: 10 });
    check("debug option", a.debug && !b.debug);
    if (host) {
        __nativeDraws();
        a.draw();
        b.draw();
        check("overlay on the debug instance only", __nativeDraws().lineLists === 1);
        check("setDebug returns the previous state", Sprite.setDebug(true) === false);
        Sprite.drawAll([a, b]);
        check("overlays for every instance", __nativeDraws().lineLists === 2);
        check("setDebug off", Sprite.setDebug(false) === true);
        Sprite.drawAll([b]);
        check("no overlay", __nativeDraws().lineLists === 0);
    } else {
        Sprite.setDebug(false);
    }
}

function animatorQueries({ grid }) {
    grid.addClip("pop", { frames: "8-9", fps: 10, mode: "once" });
    const buffer = TileMap.SpriteBuffer.create(3);
    const instance = host ? new TileMap.Instance({ spriteBuffer: buffer }) :
        new TileMap.Instance({
            descriptor: new TileMap.Descriptor({ textures: [TEXTURE], materials: [{ endOffset: 2 }] }),
            spriteBuffer: buffer,
        });
    const anim = Animator.bind(instance, grid, "pop");
    check("playing at the start", anim.isPlaying(0) && !anim.isFinished(0) && anim.finishedCount === 0);
    anim.play("spin", { first: 2, count: 1 });
    Sprite.update(0.5);
    check("finished once", anim.isFinished(0) && !anim.isPlaying(1) && anim.finishedCount === 2 &&
        anim.isPlaying(2));
    check("realTime getter", anim.realTime === false);
    throws("index range", () => anim.isFinished(3), RangeError);
    anim.unbind();
}

async function main() {
    const s = sheets();
    const file = json();
    const hero = playback(s);
    drawing(s);
    const looping = await loopSystem(s);
    const anim = animator(s);
    const chained = chaining();
    await promises(chained);
    batches(s);
    slices();
    await realTime(s);
    safety(s);
    turnedFrames();
    gridLines(s);
    await loading();
    debugOverlay(s);
    animatorQueries(s);
    garbage(s);
    // Left alive until the runtime ends: teardown must free them.
    globalThis.__keep = { s, file, hero, looping, anim };

    console.log(`Result: ${passed} passed, ${failed} failed` + (host ? "" : " (host-only checks skipped)"));
    if (!failed) console.log("Sprite module test passed");
}

main().catch(error => {
    console.log("[FAIL] " + error + "\n" + (error && error.stack));
});
