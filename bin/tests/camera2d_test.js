/*
 * Camera2D module: the binding (properties, validation, follow, timed
 * changes and their promises, zones, culling), its Loop system (the current
 * camera applied around draw, effects drawn at its end), split screen and
 * transitions. Runs on PCSX2/PS2 and on the host (tests/js/run.sh), where a
 * stub of the GS clip rectangle lets the script see the view the camera
 * sets (__view) and resize the screen (__setScreen); those checks are
 * skipped on the console.
 */
import * as Loop from "Loop";
import * as DebugNative from "DebugNative";

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
}

const near = (a, b, eps = 1e-3) => Math.abs(a - b) <= eps;
const host = typeof globalThis.__view === "function";
const { Camera, main } = Camera2D;
const BLACK = 0x80000000;

/* Runs `count` whole frames of the Loop, calling draw(i) in each; stops in the next update. */
function frames(count, draw) {
    return new Promise(resolve => {
        let frame = 0;
        Loop.run({
            update() {
                if (frame === count) {
                    Loop.stop();
                    resolve();
                }
            },
            draw() {
                if (draw) draw(frame);
                frame++;
            },
        });
    });
}

function basics() {
    check("Camera2D global", typeof Camera2D === "object" && typeof Camera === "function");
    check("main is a Camera", main instanceof Camera);
    check("main is current", Camera2D.getCurrent() === main && main.isCurrent);

    // A new camera is the identity: the screen, from (0, 0) at zoom 1.
    const vp = main.viewport;
    check("full-screen viewport", vp.x === 0 && vp.y === 0 && vp.w > 0 && vp.h > 0);
    check("identity position", main.x === vp.w / 2 && main.y === vp.h / 2 && main.zoom === 1);
    const m = main.getMatrix();
    check("identity matrix", m.xx === 1 && m.yy === 1 && m.xy === 0 && m.yx === 0 && m.tx === 0 && m.ty === 0);
    const p = main.worldToScreen(10, 20);
    check("identity worldToScreen", p.x === 10 && p.y === 20);

    const cam = new Camera({ x: 100, y: 50, zoom: 2, pixelSnap: false });
    const s = cam.worldToScreen(110, 50);
    check("zoomed worldToScreen", near(s.x, vp.w / 2 + 20) && near(s.y, vp.h / 2));
    const w = cam.screenToWorld(s.x, s.y);
    check("screenToWorld round trip", near(w.x, 110) && near(w.y, 50));
    const r = cam.visibleRect();
    check("visibleRect at zoom 2", near(r.w, vp.w / 2) && near(r.h, vp.h / 2) && near(r.x, 100 - vp.w / 4));
    check("isVisible", cam.isVisible(100, 50) && !cam.isVisible(r.x + r.w + 10, 50, 5, 5));

    cam.rotation = Math.PI / 2;
    const rs = cam.worldToScreen(100, 60);
    check("rotated worldToScreen", near(rs.x, vp.w / 2 + 20, 1e-2) && near(rs.y, vp.h / 2, 1e-2));
    cam.rotation = 0;

    // Chaining and setters.
    check("setters chain", cam.setPosition(0, 0).setZoom(1, 3).move(5, 6) === cam);
    check("setZoom per axis", cam.zoomX === 1 && cam.zoomY === 3 && cam.x === 5 && cam.y === 6);
    cam.anchor = [0, 0];
    check("anchor", cam.anchor.x === 0 && cam.anchor.y === 0);
    cam.setViewport(10, 20, 100, 50);
    check("viewport", JSON.stringify(cam.viewport) === '{"x":10,"y":20,"w":100,"h":50}');
    cam.viewport = null;
    check("viewport reset", cam.viewport.w === vp.w);
    cam.setBounds({ x: 0, y: 0, w: 1000, h: 800 });
    check("bounds", cam.bounds.w === 1000 && cam.bounds.h === 800);
    cam.bounds = null;
    check("bounds cleared", cam.bounds === null);

    // Bounds of a turned camera: its bounding box, or the unturned view.
    const turned = new Camera({ bounds: { x: 0, y: 0, w: 2000, h: 2000 }, rotation: 0.8,
        boundsIgnoreRotation: true, pixelSnap: false });
    turned.setPosition(0, 1000).move(0, 0);   // move() clamps
    check("boundsIgnoreRotation option", turned.boundsIgnoreRotation === true &&
        near(turned.x, vp.w / 2));
    turned.boundsIgnoreRotation = false;      // clamps again, by the bounding box
    check("turned view pushed inward", turned.x > vp.w / 2 + 1);

    // A viewport alone places the camera at its center: its own identity.
    const half = new Camera({ viewport: { x: 320, y: 0, w: 320, h: 448 } });
    const o = half.worldToScreen(0, 0);
    check("viewport camera shows world (0, 0) at its corner", o.x === 320 && o.y === 0);
}

function validation() {
    throws("zoom 0", () => new Camera({ zoom: 0 }), RangeError);
    throws("zoom NaN", () => { main.zoom = NaN; }, RangeError);
    throws("x Infinity", () => { main.x = Infinity; }, RangeError);
    throws("x string", () => { main.x = "5"; }, TypeError);
    throws("options not an object", () => new Camera(5), TypeError);
    throws("follow a number", () => main.follow(5), TypeError);
    throws("follow nothing", () => main.follow([]), RangeError);
    throws("follow array of numbers", () => main.follow([1, 2]), TypeError);
    throws("follow a target without x", () => new Camera().follow({ X: 1, y: 2 }), TypeError);
    throws("follow a target with a string y", () => new Camera().follow({ x: 1, y: "2" }), TypeError);
    throws("negative lerp", () => new Camera().follow({ x: 0, y: 0 }, { lerp: -1 }), RangeError);
    throws("autoZoom min above max", () => new Camera().follow([{ x: 0, y: 0 }], { autoZoom: { min: 3, max: 2 } }),
        RangeError);
    throws("empty viewport", () => main.setViewport(0, 0, 0, 10), RangeError);
    throws("anchor out of range", () => { main.anchor = 2; }, RangeError);
    throws("too many zones", () => new Camera().setZones(new Array(33).fill({ x: 0, y: 0, w: 1, h: 1 })), RangeError);
    throws("zone without size", () => new Camera().setZones([{ x: 0, y: 0 }]), TypeError);
    throws("onChange not a function", () => new Camera().setZones([], { onChange: 1 }), TypeError);
    throws("fade color", () => main.fade("black", 1), TypeError);
    throws("letterbox amount", () => main.letterbox(0.7), RangeError);
    throws("negative duration", () => main.zoomTo(2, -1), RangeError);
    throws("cull array", () => main.cull([0, 0, 1, 1]), TypeError);
    throws("cull output too short", () => main.cull(new Float32Array(8), new Uint8Array(1)), RangeError);
    throws("draw without a function", () => main.draw(1), TypeError);
    throws("setCurrent a plain object", () => Camera2D.setCurrent({}), TypeError);
    throws("transition to nothing", () => Camera2D.transition(null, 1), TypeError);
    throws("end without begin", () => new Camera().end(), Error);
    throws("Camera method on another object", () => Camera.prototype.snap.call({}), TypeError);
    check("main untouched by the failures", main.zoom === 1 && main.x === main.viewport.w / 2);
}

async function followAndPromises() {
    const player = { x: 1000, y: 300 };
    const cam = new Camera({ pixelSnap: false });
    cam.follow(player);
    check("follow snaps", cam.x === 1000 && cam.y === 300 && cam.following);

    // Frame-rate independent smoothing: the same second at 60 and 30 fps.
    const a = new Camera(), b = new Camera();
    const target = { x: 1320, y: 224 };
    a.setPosition(320, 224).follow(target, { lerp: 5, snap: false });
    b.setPosition(320, 224).follow(target, { lerp: 5, snap: false });
    for (let i = 0; i < 60; i++) a.update(1 / 60);
    for (let i = 0; i < 30; i++) b.update(1 / 30);
    const expected = 1320 - 1000 * Math.exp(-5);
    check("smoothing at 60 fps", near(a.x, expected, 0.1));
    check("smoothing at 30 fps", near(b.x, expected, 0.1));

    // Several targets: their center, zoomed to fit.
    const p1 = { x: 0, y: 0 }, p2 = { x: 800, y: 0 };
    const pair = new Camera().follow([p1, p2, { x: "gone" }], { autoZoom: { margin: 20, lerp: 0 } });
    pair.update(1 / 60);
    const room = pair.viewport.w - 40;
    check("multi-target center", near(pair.x, 400));
    check("auto zoom fits", near(pair.zoom, Math.min(2, Math.max(0.5, room / 800)), 1e-3));
    pair.unfollow();
    check("unfollow", !pair.following);

    // Timed changes resolve with true; a replaced one with false.
    const results = [];
    const first = cam.zoomTo(2, 0.5).then(ok => results.push(["first", ok]));
    const second = cam.zoomTo(4, 0.2).then(ok => results.push(["second", ok]));
    const instant = cam.letterbox(0.1).then(ok => results.push(["instant", ok]));
    for (let i = 0; i < 20; i++) cam.update(1 / 60);
    await Promise.all([first, second, instant]);
    check("replaced zoom resolves false", results.some(r => r[0] === "first" && r[1] === false));
    check("zoom resolves true", results.some(r => r[0] === "second" && r[1] === true) && cam.zoom === 4);
    check("instant letterbox", results.some(r => r[0] === "instant" && r[1] === true) &&
        near(cam.letterboxAmount, 0.1, 1e-6));

    const faded = cam.fade(BLACK, 0.1);
    for (let i = 0; i < 10; i++) cam.update(1 / 60);
    check("fade resolves", await faded === true && cam.fadeAlpha === 128);
    const panned = cam.panTo(0, 0, 0.1);
    for (let i = 0; i < 10; i++) cam.update(1 / 60);
    check("pan resolves", await panned === true);
    cam.update(1 / 60);
    check("follow resumes after the pan", cam.x === 1000 && cam.y === 300);

    cam.shake(5, 0.2, { frequency: 30, rotation: 0.02 });
    cam.update(1 / 60);
    check("shaking", cam.shaking);
    cam.stopShake();
    check("shake stopped", !cam.shaking);

    // Zones: the room holding the target bounds the camera; onChange reports it.
    const hero = { x: 100, y: 100 };
    const changes = [];
    const rooms = new Camera().setZones([
        { x: 0, y: 0, w: 640, h: 448 },
        { x: 640, y: 0, w: 1280, h: 448, zoom: 2 },
    ], { onChange(zone) { changes.push([this === rooms, zone]); } });
    rooms.follow(hero);
    rooms.update(1 / 60);
    hero.x = 700;
    rooms.update(1 / 60);
    check("zone callbacks", JSON.stringify(changes) === "[[true,0],[true,1]]" && rooms.zone === 1);
    check("zone zoom and bounds", rooms.zoom === 2 && rooms.x === 640 + rooms.viewport.w / 4);

    // Culling in one call.
    const view = new Camera({ pixelSnap: false });
    const vr = view.visibleRect();
    const boxes = new Float32Array([vr.x, vr.y, 10, 10, vr.x + vr.w + 1, 0, 10, 10, -20, vr.y, 21, 5]);
    const out = new Uint8Array(3);
    check("cull", view.cull(boxes, out) === 2 && out[0] === 1 && out[1] === 0 && out[2] === 1);
    check("cull without output", view.cull(boxes) === 2);
    // Unaligned Float32Array views are read safely.
    const raw = new ArrayBuffer(4 + 16);
    const shifted = new Float32Array(raw, 4, 4);
    shifted.set([0, 0, 1, 1]);
    check("cull unaligned-offset view", view.cull(shifted) === 1);
}

async function loopTests() {
    const vp = main.viewport;
    const player = { x: 900, y: 500 };
    let inside = null, hud = null, split = null, after = null;
    main.follow(player);
    check("camera system registered", true);

    await frames(2, frame => {
        if (!host) return;
        inside = __view();
        Camera2D.screenSpace(() => { hud = __view(); });
    });
    if (host) {
        after = __view();
        check("current camera applied in draw", inside.tx === Math.round(vp.w / 2 - 900) &&
            inside.ty === Math.round(vp.h / 2 - 500) && inside.world === inside.tx);
        check("screenSpace is the identity", hud.kind === 0 && hud.clip[2] === vp.w);
        check("view restored after draw", after.kind === 0 && after.clip[2] === vp.w);
    }
    // Debug maps world shapes and texts with the world view the camera publishes.
    const published = DebugNative.worldView();
    check("world view for Debug", published !== null && published[0] === 1 &&
        published[4] === Math.round(vp.w / 2 - 900) && published[5] === Math.round(vp.h / 2 - 500));
    // Stopped inside draw, the frame still ends the camera (postDraw runs).
    await new Promise(resolve => Loop.run({ draw() { Loop.stop(); resolve(); } }));
    if (host) check("stopping in draw closes the camera", __view().kind === 0);

    // Split screen: no current camera, one camera per half.
    const left = new Camera({ viewport: { x: 0, y: 0, w: vp.w / 2, h: vp.h } });
    const right = new Camera({ viewport: { x: vp.w / 2, y: 0, w: vp.w / 2, h: vp.h } });
    left.follow({ x: 0, y: 0 });
    right.follow({ x: 5000, y: 0 });
    Camera2D.setCurrent(null);
    check("no current camera", Camera2D.getCurrent() === null && !main.isCurrent);
    await frames(1, () => {
        if (!host) return;
        split = [];
        left.draw(() => split.push(__view()));
        right.draw(() => split.push(__view()));
        right.viewportSpace(() => split.push(__view()));
        split.push(__view());
    });
    if (host) {
        check("left viewport clip", JSON.stringify(split[0].clip) === JSON.stringify([0, 0, vp.w / 2, vp.h]));
        check("right viewport clip", split[1].clip[0] === vp.w / 2 && split[1].tx === Math.round(vp.w * 0.75 - 5000));
        check("viewportSpace offsets to the viewport", split[2].tx === vp.w / 2 && split[2].kind === 1 &&
            split[2].clip[0] === vp.w / 2);
        check("nothing applied without a current camera", split[3].kind === 0 && split[3].world === null);
    }
    check("no world view without a current camera", DebugNative.worldView() === null);

    // Parallax: a layer at 0 stays in screen space.
    let layer = null;
    main.makeCurrent();
    await frames(1, () => { if (host) main.draw(() => { layer = __view(); }, { parallax: 0 }); });
    if (host) check("parallax 0 layer", layer.kind === 0);

    // Effects are drawn at the end of the camera, in its viewport.
    if (host) __nativeDraws();
    main.fade(BLACK, 0);
    main.letterbox(0.1, 0);
    await frames(1);
    if (host) check("fade and letterbox drawn", __nativeDraws().sprites === 3);
    main.fade(0, 0);
    main.letterbox(0, 0);
    await frames(1);
    if (host) check("no effects, no draws", __nativeDraws().sprites === 0);

    // Transitions glide between cameras and make the target current.
    const other = new Camera().follow({ x: 3000, y: 500 });
    let middle = null;
    const done = Camera2D.transition(other, 0.1, { ease: t => t });
    check("transition target is current", Camera2D.getCurrent() === other);
    await frames(8, frame => { if (host && frame === 2) middle = __view(); });
    check("transition resolves", await done === true && other.isCurrent);
    if (host) {
        const from = Math.round(vp.w / 2 - 900), to = Math.round(vp.w / 2 - 3000);
        check("transition in between", middle.tx < from && middle.tx > to);
    }
    const cut = Camera2D.transition(main, 1);
    Camera2D.setCurrent(main);
    check("interrupted transition resolves false", await cut === false);

    // The Loop's time scale reaches the cameras.
    Loop.setTimeScale(0);
    const frozen = new Camera().setPosition(0, 0).follow({ x: 999, y: 0 }, { lerp: 5, snap: false });
    await frames(3);
    check("paused game, paused camera", frozen.x === 0);
    Loop.setTimeScale(1);
    await frames(3);
    check("resumed", frozen.x > 0);

    main.unfollow();
    Camera2D.reset();
    if (host) {
        const reset = __view();
        check("reset", reset.kind === 0 && reset.world === null && reset.clip[2] === vp.w);
    }
}

// Loop phases by hand (host only): UPDATE, POST_UPDATE, PRE_DRAW, POST_DRAW.
const PHASE = { update: 1, postUpdate: 2, preDraw: 3, postDraw: 4 };

async function newFeatures() {
    const vp = main.viewport;

    // interpolate: the camera follows the target blended by the Loop's alpha.
    if (host) {
        const t = { x: 100, y: 0 };
        const cam = new Camera({ pixelSnap: false }).follow(t, { interpolate: true });
        const plain = new Camera({ pixelSnap: false }).follow(t);
        __runNativeSystems(PHASE.update, 1 / 60, 1 / 60);      // before the step: 100
        t.x = 200;                                            // the step
        __runNativeSystems(PHASE.postUpdate, 1 / 60, 1 / 60);
        check("non-interpolated cameras move in postUpdate", plain.x === 200);
        check("interpolated ones wait for preDraw", cam.x === 100);
        __runNativeSystems(PHASE.preDraw, 0.5, 0.5);
        __runNativeSystems(PHASE.postDraw, 0.5, 0.5);
        check("interpolate: halfway at alpha 0.5", near(cam.x, 150));
        cam.unfollow(); plain.unfollow();
    }

    // realTime: keeps moving and fading while the game is paused.
    Loop.setTimeScale(0);
    const scaled = new Camera().setPosition(0, 0).follow({ x: 999, y: 0 }, { lerp: 5, snap: false });
    const real = new Camera({ realTime: true }).setPosition(0, 0)
        .follow({ x: 999, y: 0 }, { lerp: 5, snap: false });
    const realFade = real.fade(BLACK, 0.05);
    await frames(6);
    Loop.setTimeScale(1);
    check("realTime option", real.realTime === true && scaled.realTime === false);
    check("paused: scaled camera frozen, real-time one moves", scaled.x === 0 && real.x > 0);
    check("real-time fade finishes while paused", await realFade === true);
    scaled.unfollow(); real.unfollow();

    // zoomBySpeed: zooms out while the target runs, back when it stops.
    const runner = { x: 0, y: 0 };
    const fast = new Camera().follow(runner, { zoomBySpeed: { min: 0.5, max: 1, speed: 300, lerp: 0 } });
    for (let i = 0; i < 120; i++) { runner.x += 10; fast.update(1 / 60); }
    check("zoomBySpeed out while running", near(fast.zoom, 0.5, 0.01));
    for (let i = 0; i < 300; i++) fast.update(1 / 60);
    check("zoomBySpeed back at rest", near(fast.zoom, 1, 0.01));
    throws("zoomBySpeed min above max", () => new Camera().follow(runner, { zoomBySpeed: { min: 2, max: 1 } }),
        RangeError);

    // Trauma and kick.
    const hit = new Camera({ pixelSnap: false });
    hit.addTrauma(0.5).addTrauma(0.3, { intensity: 20 });
    check("trauma adds up", near(hit.trauma, 0.8, 1e-5));
    hit.kick(10, 0, 0.2).update(0);
    check("kick moves the view", near(hit.getMatrix().tx, 10, 12));
    hit.stopShake().update(0);
    check("stopShake stops trauma and kick", hit.trauma === 0 && hit.getMatrix().tx === 0);
    throws("addTrauma above 1", () => hit.addTrauma(2), RangeError);

    // Zones with their own offset and lerp, and the previous zone.
    const walker = { x: 100, y: 100 };
    const calls = [];
    const rooms = new Camera().setZones([
        { x: 0, y: 0, w: 2000, h: 1000 },
        { x: 2000, y: 0, w: 2000, h: 1000, offset: [0, -50], lerp: 0 },
    ], { onChange(zone, previous) { calls.push([zone, previous]); } });
    rooms.follow(walker, { lerp: 1 }).update(1 / 60);
    walker.x = 3000; walker.y = 500;
    rooms.update(1 / 60);
    check("onChange(zone, previous)", JSON.stringify(calls) === "[[0,-1],[1,0]]");
    check("zone offset and lerp", rooms.x === 3000 && rooms.y === 450);

    // push/pop: back to the camera replaced.
    const cutscene = new Camera();
    Camera2D.setCurrent(main);
    check("push", await Camera2D.push(cutscene) === true && Camera2D.getCurrent() === cutscene &&
        Camera2D.getStats().pushed === 1);
    check("pop", await Camera2D.pop() === true && Camera2D.getCurrent() === main &&
        Camera2D.getStats().pushed === 0);
    throws("pop without push", () => Camera2D.pop(), RangeError);
    Camera2D.setCurrent(null);
    Camera2D.push(cutscene);
    Camera2D.pop();
    check("push over no camera pops back to none", Camera2D.getCurrent() === null);
    Camera2D.setCurrent(main);
    const glide = Camera2D.push(cutscene, { duration: 0.05, realTime: true });
    await frames(6);
    check("push with a transition", await glide === true && cutscene.isCurrent);
    await Camera2D.pop({ duration: 0 });

    // state()/setState(): JSON round trip.
    const saved = new Camera({ x: 12, y: 34, zoom: 2, rotation: 0.5, bounds: { x: 0, y: 0, w: 500, h: 400 },
        viewport: { x: 10, y: 10, w: 200, h: 100 }, boundsIgnoreRotation: true, realTime: true });
    const json = JSON.parse(JSON.stringify(saved.state()));
    const restored = new Camera().setState(json);
    check("state round trip", JSON.stringify(restored.state()) === JSON.stringify(saved.state()));
    check("full-screen viewport saves as null", new Camera().state().viewport === null);
    throws("setState needs an object", () => restored.setState(5), TypeError);

    // drawRepeat: copies to cover the view, and a limit.
    const tile = { width: 100, height: 50, drawn: [], draw(x, y) { this.drawn.push([x, y]); } };
    const sky = new Camera();
    const copies = sky.drawRepeat(tile, { parallax: 0 });
    const expected = (Math.floor(vp.w / 100) + 1) * (Math.floor(vp.h / 50) + 1);
    check("drawRepeat covers the view", copies === expected && tile.drawn.length === expected &&
        tile.drawn[0][0] === 0 && tile.drawn[0][1] === 0);
    tile.drawn.length = 0;
    check("drawRepeat one row", sky.drawRepeat(tile, { repeatY: false, y: 20 }) === Math.floor(vp.w / 100) + 1 &&
        tile.drawn.every(p => p[1] === 20));
    throws("drawRepeat with too many copies", () => sky.drawRepeat({ width: 1, height: 1, draw() {} }), RangeError);
    throws("drawRepeat without an image", () => sky.drawRepeat({}), TypeError);

    // debug: the overlay goes out at end().
    const shown = new Camera({ debug: true }).setBounds(0, 0, 1000, 1000);
    shown.follow({ x: 500, y: 500 }, { deadzone: 40 });
    if (host) __nativeDraws();
    shown.draw(() => {});
    if (host) {
        const drawn = __nativeDraws();
        check("debug overlay drawn", shown.debug && drawn.lines >= 8 && drawn.circles >= 2);
    }
    shown.unfollow();

    // getStats: counters are numbers (culling is counted by the real draws).
    await frames(1);
    check("getStats", typeof Camera2D.getStats().culled === "number");
}

async function robustness() {
    check("toStringTag", Object.prototype.toString.call(main) === "[object Camera2D.Camera]");

    // draw() returns what its callback returns, and closes what it left open.
    const zoomed = new Camera({ zoom: 2 });
    const value = zoomed.draw(() => { new Camera({ zoom: 3 }).begin(); return 42; });
    check("draw returns the callback's value", value === 42);
    check("screenSpace returns too", Camera2D.screenSpace(() => "hud") === "hud");
    if (host) check("pairs left open by the callback are closed", __view().kind === 0);
    let thrown = null;
    try { zoomed.draw(() => { throw new Error("inside draw"); }); } catch (e) { thrown = e; }
    check("draw rethrows", thrown && thrown.message === "inside draw");
    if (host) check("and restores the view", __view().kind === 0);

    // Fractional world sizes under a zoomed camera (on the console: real Draw).
    if (!host) {
        let error = null;
        try { zoomed.draw(() => Draw.rect(10, 10, 0.5, 0.5, BLACK)); } catch (e) { error = e; }
        check("Draw.rect takes fractions under a camera", error === null);
    }

    // A camera nobody keeps, but awaited: it lives until its promise settles.
    let answer = null;
    (() => { new Camera().panTo(100, 0, 0.05).then(ok => { answer = ok; }); })();
    std.gc();
    await frames(8);
    check("an awaited camera still answers after a GC", answer === true);
}

async function lifetimes() {
    // Cameras that follow themselves or each other must still be collected.
    for (let i = 0; i < 50; i++) {
        const cam = new Camera();
        const target = { x: i, y: i, cam };
        cam.follow(target);
        cam.zoomTo(2, 1);          // a pending promise holds its resolve function
        cam.setZones([{ x: 0, y: 0, w: 10, h: 10 }], { onChange() { return cam; } });
    }
    std.gc();
    await frames(2);
    check("cameras collected without errors", true);

    // A camera kept alive until the end of the script, following a cycle.
    const keeper = new Camera();
    const hero = { x: 0, y: 0, keeper };
    keeper.follow(hero);
    keeper.fade(BLACK, 10);        // never settles: dropped at teardown
    globalThis.__keeper = keeper;

    // A throwing target getter surfaces its own error: in follow(), which
    // reads the target at once, and in later updates.
    const trap = new Camera();
    let armed = false;
    const target = { get x() { if (armed) throw new Error("target getter"); return 0; }, y: 0 };
    trap.follow(target);
    armed = true;
    let error = null;
    try { trap.update(1 / 60); } catch (e) { error = e; }
    check("target getter error surfaces in update", error && String(error).includes("target getter"));
    error = null;
    try { trap.follow(target); } catch (e) { error = e; }
    check("target getter error surfaces in follow", error && String(error).includes("target getter"));
    trap.unfollow();
}

if (host) {
    // A PAL-sized screen before any camera reads it: new cameras follow it.
    __setScreen(640, 512);
    check("screen size read per camera", new Camera().viewport.h === 512);
    // Until something positions it, a camera stays the identity view.
    check("unplaced main follows the screen", main.y === 256 && main.getMatrix().ty === 0);
    __setScreen(640, 448);
    check("and back", main.y === 224);
}
basics();
validation();
followAndPromises()
    .then(loopTests)
    .then(newFeatures)
    .then(robustness)
    .then(lifetimes)
    .catch(error => { failed++; console.log("[FAIL] " + error + "\n" + (error.stack || "")); })
    .then(() => {
        console.log(`Result: ${passed} passed, ${failed} failed` + (host ? "" : " (host-only checks skipped)"));
        if (!failed) console.log("Camera2D module test passed");
    });
