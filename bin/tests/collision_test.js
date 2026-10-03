/*
 * Collision module: worlds and bodies, swept movement against bodies and
 * tiles (one-way platforms, slopes), the physics step, moving platforms,
 * contacts, queries, pairs, raycasts, the stateless shape tests, the Loop
 * system and garbage collection of worlds and bodies. Runs on PCSX2/PS2 and
 * on the host (tests/js/run.sh), where the Draw stubs count the debug lines
 * (__nativeDraws); those checks are skipped on the console. Some objects are
 * left alive at the end on purpose: teardown must free them without leaks.
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

const near = (a, b, eps = 1e-2) => Math.abs(a - b) <= eps;
const host = typeof globalThis.__nativeDraws === "function";
const { World, Body } = Collision;
const DT = 1 / 60;

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

/* A 16 px grid from rows of characters: '#' solid, '-' one-way, '/' and '\\' slopes. */
function level(world, rows, extra = {}) {
    const tiles = [];
    for (const row of rows)
        for (const ch of row)
            tiles.push(ch === "#" ? 1 : ch === "-" ? 2 : ch === "/" ? 3 : ch === "\\" ? 4 : 0);
    world.setGrid(Object.assign({
        columns: rows[0].length, rows: rows.length, tileWidth: 16, tileHeight: 16,
        tiles, solid: [1], oneWay: [2], slopes: { 3: "45r", 4: "45l" },
    }, extra));
}

function basics() {
    check("Collision global", typeof Collision === "object" && typeof World === "function" &&
        typeof Body === "function");
    throws("World without new", () => World(), TypeError);
    throws("Body constructor", () => new Body(), TypeError);
    throws("bad cellSize", () => new World({ cellSize: 0 }), RangeError);
    throws("bad gravity", () => new World({ gravity: 5 }), TypeError);

    const world = new World({ gravity: [0, 500], autoStep: false, cellSize: 32 });
    check("world settings", world.gravityX === 0 && world.gravityY === 500 &&
        world.cellSize === 32 && world.autoStep === false && world.bodyCount === 0 &&
        world.grid === undefined && world.onContact === null);
    world.gravityX = 10;
    check("gravity setter", world.gravityX === 10);
    throws("gravity NaN", () => { world.gravityY = NaN; }, RangeError);
    check("rejected setter keeps the value", world.gravityY === 500);

    const box = world.add({ x: 10, y: 20, w: 30, h: 40 });
    const ball = world.add({ type: "dynamic", x: 100, y: 100, r: 8, layer: 4, mask: 1 | 4,
        bounce: 0.5, damping: 1, gravityScale: 2, maxSpeedX: 50, sensor: true });
    check("rect defaults", box instanceof Body && box.shape === "rect" && box.type === "static" &&
        box.x === 10 && box.y === 20 && box.w === 30 && box.h === 40 && box.r === undefined &&
        box.layer === 1 && box.mask === -1 && !box.sensor && !box.oneWay && box.gravityScale === 1 &&
        box.world === world && box.valid);
    check("circle", ball.shape === "circle" && ball.r === 8 && ball.w === 16 && ball.type === "dynamic" &&
        ball.layer === 4 && ball.mask === 5 && ball.bounce === 0.5 && ball.damping === 1 &&
        ball.gravityScale === 2 && ball.maxSpeedX === 50 && ball.sensor);
    check("bodies()", world.bodyCount === 2 && world.bodies().length === 2 &&
        world.bodies().includes(box) && world.bodies().includes(ball));
    const bounds = ball.getBounds();
    check("getBounds", bounds.x === 92 && bounds.y === 92 && bounds.w === 16 && bounds.h === 16);

    throws("add without size", () => world.add({ x: 0, y: 0 }), TypeError);
    throws("add zero size", () => world.add({ w: 0, h: 5 }), RangeError);
    throws("add bad type", () => world.add({ w: 1, h: 1, type: "rigid" }), TypeError);
    throws("add infinite", () => world.add({ w: 1, h: 1, x: Infinity }), RangeError);
    throws("add bounce > 1", () => world.add({ w: 1, h: 1, bounce: 2 }), RangeError);
    throws("add without options", () => world.add(), TypeError);
    throws("circle w", () => { ball.w = 3; }, TypeError);
    throws("rect r", () => { box.r = 3; }, TypeError);
    throws("read-only", () => { box.onGround = true; }, TypeError);
    box.type = "kinematic";
    box.layer = 1 << 31;
    check("setters", box.type === "kinematic" && box.layer === (1 << 31));
    box.type = "static";
    box.layer = 1;
    box.x = 500;
    check("teleport updates queries", world.queryPoint(510, 30).includes(box) &&
        !world.queryPoint(20, 30).includes(box));

    const other = new World({ autoStep: false });
    check("remove foreign body", other.remove(box) === false);
    throws("move a foreign body", () => other.move(box, 1, 0), TypeError);
    check("remove", world.remove(ball) === true && !ball.valid && ball.world === null &&
        world.bodyCount === 1);
    check("remove twice", world.remove(ball) === false && ball.remove() === false);
    throws("removed body", () => ball.x, TypeError);
    check("world.bodies after remove", world.bodies().length === 1);
    world.clear();
    check("clear", world.bodyCount === 0 && !box.valid);
}

function movement() {
    const world = new World({ autoStep: false });
    const wall = world.add({ x: 100, y: 0, w: 20, h: 200 });
    const box = world.add({ type: "dynamic", x: 50, y: 50, w: 20, h: 20 });

    let r = world.move(box, 100, 0);
    check("stops at the wall", near(box.x, 80) && r.hitWall === 1 && near(r.dx, 30) && r.dy === 0);
    check("contact", r.contacts.length === 1 && r.contacts[0].body === wall &&
        r.contacts[0].tile === -1 && r.contacts[0].normalX === -1 && r.contacts[0].normalY === 0);
    check("body state", box.onWall === 1 && !box.onGround && !box.onCeiling && box.ground === null);
    r = box.move(5, 30);
    check("slides along", near(box.x, 80) && near(box.y, 80) && r.hitWall === 1);
    world.add({ x: 0, y: 300, w: 400, h: 1 });
    box.setPosition(10, 200);
    r = box.move(0, 100000);
    check("no tunneling", near(box.y, 280) && r.onGround && box.onGround);
    box.mask = 2;
    box.setPosition(50, 50);
    box.move(100, 0);
    check("mask passes the wall", near(box.x, 150));
    throws("move NaN", () => box.move(NaN, 0), RangeError);
}

function tiles() {
    const world = new World({ autoStep: false, gravity: { x: 0, y: 900 } });
    level(world, [
        "..........",
        "..........",
        "....--....",
        "..........",
        "#........#",
        "##########",
    ]);
    const g = world.grid;
    check("grid getter", g.columns === 10 && g.rows === 6 && g.tileWidth === 16 && g.layer === 1);
    check("getTile", world.getTile(0, 5) === 1 && world.getTile(4, 2) === 2 && world.getTile(-1, 0) === -1);
    const cell = world.cellAt(70, 40);
    check("cellAt", cell.column === 4 && cell.row === 2 && cell.tile === 2 && world.cellAt(-1, 0) === null);
    check("setTile", world.setTile(4, 0, 1) === true && world.getTile(4, 0) === 1 &&
        world.setTile(99, 0, 1) === false);
    world.setTile(4, 0, 0);

    const p = world.add({ type: "dynamic", x: 40, y: 20, w: 12, h: 14 });
    for (let i = 0; i < 120; i++) world.step(DT);
    check("lands on the floor", p.onGround && near(p.y + p.h, 80) && p.vy === 0);
    for (let i = 0; i < 60; i++) { p.vx = 200; world.step(DT); }
    check("right wall", near(p.x + p.w, 144) && p.onWall === 1);

    p.setPosition(70, 66);
    p.vx = 0;
    p.vy = -400;
    for (let i = 0; i < 12; i++) world.step(DT);
    check("jumps through the one-way", p.y + p.h < 32);
    for (let i = 0; i < 120; i++) world.step(DT);
    check("lands on the one-way", p.onGround && near(p.y + p.h, 32));
    p.dropThrough = true;
    for (let i = 0; i < 30; i++) world.step(DT);
    p.dropThrough = false;
    check("drops through", near(p.y + p.h, 80));

    // Uint16Array tiles, a slope and a layer that the mask leaves out.
    const ids = new Uint16Array(10 * 3);
    ids.fill(1, 20);
    ids[14] = 3;
    world.setGrid({ columns: 10, rows: 3, tileWidth: 16, tileHeight: 16, tiles: ids,
        solid: [1], slopes: { 3: [0, 1] }, layer: 2 });
    check("Uint16Array tiles", world.getTile(4, 1) === 3 && world.getTile(0, 2) === 1);
    ids[0] = 1;
    check("tiles are copied", world.getTile(0, 0) === 0);
    p.setPosition(40, 0);
    p.vy = 0;
    p.mask = 1;
    for (let i = 0; i < 30; i++) world.step(DT);
    check("grid layer not in the mask", p.y > 100);
    p.mask = -1;
    p.setPosition(24, 18);
    p.vy = 0;
    world.step(DT);
    check("on the new floor", p.onGround && near(p.y + p.h, 32));
    for (let i = 0; i < 30; i++) { p.vx = 60; world.step(DT); }
    check("walks up the slope", p.onGround && p.y + p.h < 32 && p.x > 40);

    throws("grid without size", () => world.setGrid({ columns: 2 }), TypeError);
    throws("tiles length", () => world.setGrid({ columns: 2, rows: 2, tileWidth: 8, tileHeight: 8,
        tiles: [1, 2, 3] }), RangeError);
    throws("unknown slope", () => world.setGrid({ columns: 1, rows: 1, tileWidth: 8, tileHeight: 8,
        slopes: { 1: "steep" } }), TypeError);
    throws("slope key", () => world.setGrid({ columns: 1, rows: 1, tileWidth: 8, tileHeight: 8,
        slopes: { abc: "45r" } }), RangeError);
    throws("tile id range", () => world.setGrid({ columns: 1, rows: 1, tileWidth: 8, tileHeight: 8,
        solid: [70000] }), RangeError);
    check("failed setGrid keeps the grid", world.getTile(4, 1) === 3);
    world.clearGrid();
    check("clearGrid", world.grid === undefined && world.getTile(0, 0) === -1);
}

function physics() {
    const world = new World({ autoStep: false, gravity: { x: 0, y: 1000 } });
    world.add({ x: -1000, y: 200, w: 2000, h: 20 });
    const ball = world.add({ type: "dynamic", x: 0, y: 0, r: 8, bounce: 0.5 });
    const lift = world.add({ type: "kinematic", x: 300, y: 150, w: 60, h: 10 });
    const rider = world.add({ type: "dynamic", x: 320, y: 130, w: 10, h: 20 });
    let bounces = 0;

    for (let i = 0; i < 600; i++) {
        const vy = ball.vy;
        world.step(DT);
        if (vy > 0 && ball.vy < 0) bounces++;
    }
    check("bounces then rests", bounces >= 2 && ball.onGround && ball.vy === 0 && near(ball.y, 192));
    check("rider on the platform", rider.onGround && rider.ground === lift);
    lift.vx = 60;
    lift.vy = -30;
    const x0 = rider.x;
    for (let i = 0; i < 60; i++) world.step(DT);
    check("carried", near(rider.x - x0, 60, 0.1) && near(rider.y + rider.h, lift.y, 0.1) &&
        rider.ground === lift);
    throws("step NaN", () => world.step(NaN), RangeError);
}

function contacts() {
    const world = new World({ autoStep: false });
    const wall = world.add({ x: 30, y: 0, w: 10, h: 10 });
    const a = world.add({ type: "dynamic", x: 0, y: 0, w: 10, h: 10, vx: 3000 });
    const seen = [];

    throws("onContact type", () => { world.onContact = 5; }, TypeError);
    world.onContact = function (body, contact) {
        seen.push([this, body, contact.body, contact.normalX]);
    };
    world.step(DT);
    check("onContact", seen.length === 1 && seen[0][0] === world && seen[0][1] === a &&
        seen[0][2] === wall && seen[0][3] === -1);

    // Removing bodies from the callback, and step() inside it.
    world.onContact = (body, contact) => { contact.body.remove(); };
    a.setPosition(0, 0);
    a.vx = 3000;
    world.step(DT);
    check("removed from onContact", !wall.valid && world.bodyCount === 1);
    const wall2 = world.add({ x: 30, y: 0, w: 10, h: 10 });
    world.onContact = () => world.step(DT);
    a.setPosition(0, 0);
    a.vx = 3000;
    throws("step inside onContact", () => world.step(DT), InternalError);
    world.onContact = () => { throw new Error("boom"); };
    a.setPosition(0, 0);
    a.vx = 3000;
    const error = throws("callback error", () => world.step(DT), Error);
    check("callback error message", error && error.message === "boom");
    world.onContact = null;
    check("onContact cleared", world.onContact === null && wall2.valid);
}

function queries() {
    const world = new World({ autoStep: false, cellSize: 16 });
    const a = world.add({ type: "dynamic", x: 0, y: 0, w: 10, h: 10 });
    const s = world.add({ x: 5, y: 5, w: 10, h: 10, sensor: true, layer: 4 });
    const c = world.add({ x: 100, y: 100, r: 10 });
    const bodies = [];
    for (let i = 0; i < 200; i++)
        bodies.push(world.add({ x: 200 + (i % 20) * 12, y: (i / 20 | 0) * 12, w: 10, h: 10, layer: 8 }));

    check("query", world.query(0, 0, 150, 150).length === 3);
    const byLayer = world.query(0, 0, 150, 150, 4);
    check("query by layer", byLayer.length === 1 && byLayer[0] === s);
    check("queryCircle", world.queryCircle(100, 85, 5.5)[0] === c && world.queryCircle(100, 85, 4.5).length === 0);
    check("queryPoint", world.queryPoint(7, 7).length === 2);
    check("large query", world.query(190, -10, 400, 400, 8).length === 200);
    check("overlapping", world.overlapping(a).length === 1 && world.overlapping(a)[0] === s);
    a.mask = 1;
    check("overlapping masks", world.overlapping(a).length === 0);

    const bullet = world.add({ x: 211, y: 5, r: 2, layer: 2 });
    const pairs = world.pairs(2, 8);
    check("pairs", pairs.length === 2 && pairs.every(p => p[0] === bullet && bodies.includes(p[1])));
    check("self pairs once", world.pairs(8, 8).length === 0);
    bodies[1].x = 205;
    check("self pairs", world.pairs(8, 8).length === 1);
    throws("pairs without layers", () => world.pairs(), TypeError);
    throws("query negative size", () => world.query(0, 0, -1, 5), RangeError);
}

function raycasts() {
    const world = new World({ autoStep: false });
    level(world, [
        "..........",
        "....#.....",
        "......-...",
        "../.......",
        "##########",
    ]);
    const box = world.add({ x: 200, y: 10, w: 10, h: 10, layer: 2 });
    const sensor = world.add({ x: 150, y: 10, w: 10, h: 10, sensor: true });

    let hit = world.raycast(0, 24, 160, 24);
    check("tile hit", hit && hit.tile === 1 && hit.column === 4 && hit.row === 1 && near(hit.x, 64) &&
        hit.normalX === -1 && hit.body === null && near(hit.distance, 64));
    hit = world.raycast(100, 0, 100, 100);
    check("one-way from above", hit && hit.tile === 2 && near(hit.y, 32));
    check("one-way from below", world.raycast(100, 60, 100, 0) === null);
    hit = world.raycast(40, 0, 40, 100);
    check("slope", hit && hit.tile === 3 && near(hit.y, 56) && hit.normalX < 0 && hit.normalY < 0);
    hit = world.raycast(100, 15, 300, 15);
    check("body hit", hit && hit.body === box && near(hit.x, 200) && near(hit.fraction, 0.5));
    check("sensors skipped", hit.body !== sensor);
    hit = world.raycast(100, 15, 300, 15, { sensors: true });
    check("sensors option", hit && hit.body === sensor);
    check("ignore", world.raycast(100, 15, 300, 15, { ignore: box }) === null);
    check("mask", world.raycast(100, 15, 300, 15, { mask: 1 }) === null &&
        world.raycast(0, 24, 160, 24, { mask: 2 }) === null);
    throws("raycast options", () => world.raycast(0, 0, 1, 1, 5), TypeError);
}

function shapes() {
    const a = { x: 0, y: 0, w: 10, h: 10 }, b = { x: 8, y: 3, w: 10, h: 10 };
    check("overlaps", Collision.overlaps(a, b) && !Collision.overlaps(a, { x: 10, y: 0, w: 5, h: 5 }));
    const mtv = Collision.resolve(a, b);
    check("resolve", mtv && mtv.x === -2 && mtv.y === 0);
    check("resolve none", Collision.resolve(a, { x: 50, y: 0, r: 2 }) === null);
    const circles = Collision.resolve({ x: 0, y: 0, r: 5 }, { x: 8, y: 0, r: 5 });
    check("circles", near(circles.x, -2) && near(circles.y, 0));
    const seg = Collision.segment(-10, 5, 20, 5, a);
    check("segment", seg && near(seg.fraction, 1 / 3, 1e-4) && seg.normalX === -1 && near(seg.x, 0));
    check("segment from inside", Collision.segment(5, 5, 50, 5, a) === null);
    const world = new World({ autoStep: false });
    const body = world.add({ x: 5, y: 5, r: 3 });
    check("body as shape", Collision.overlaps(body, a));
    throws("bad shape", () => Collision.overlaps(a, 5), TypeError);
}

async function loopSystem() {
    const world = new World({ gravity: { x: 0, y: 600 } });
    const manual = new World({ gravity: { x: 0, y: 600 }, autoStep: false });
    const ball = world.add({ type: "dynamic", x: 0, y: 0, r: 4 });
    const still = manual.add({ type: "dynamic", x: 0, y: 0, r: 4 });
    const hits = [];

    world.add({ x: -50, y: 40, w: 100, h: 10 });
    world.onContact = body => hits.push(body);
    check("autoStep default", world.autoStep === true);
    await frames(60);
    check("auto-stepped by the Loop", ball.onGround && near(ball.y, 36) && hits.length > 0 && hits[0] === ball);
    check("manual world stays", still.y === 0);
    if (!host) {
        const systems = Loop.getSystems().filter(s => s.name === "collision");
        check("native Loop system", systems.length === 1 && systems[0].native);
    }
    // Paused: the Loop's scaled time is 0.
    ball.setPosition(0, -100);
    ball.vy = 0;
    Loop.setTimeScale(0);
    await frames(10);
    check("paused with the Loop", ball.y === -100);
    Loop.setTimeScale(1);
    world.autoStep = false;
    await frames(10);
    check("autoStep off", ball.y === -100);
    world.autoStep = true;
    await frames(10);
    check("autoStep on again", ball.y > -100);

    // A dropped world is not stepped once collected, and nothing leaks.
    (() => {
        const lost = new World({ gravity: { x: 0, y: 10 } });
        for (let i = 0; i < 20; i++) lost.add({ type: "dynamic", x: i * 20, y: 0, w: 8, h: 8 });
        lost.onContact = () => lost;   // a closure over the world: one more cycle
    })();
    std.gc();
    await frames(3);
    return world;
}

function garbage() {
    // Bodies kept without their world, worlds kept without their bodies.
    let kept = [];
    for (let n = 0; n < 5; n++) {
        const world = new World({ autoStep: false });
        for (let i = 0; i < 50; i++) {
            const body = world.add({ type: "dynamic", x: i * 3, y: n * 3, w: 4, h: 4 });
            if (i === 7) kept.push(body);
            body.userData = { index: i, body };   // a property on the body: another cycle
        }
        world.step(DT);
    }
    std.gc();
    check("bodies keep their world", kept.every(b => b.valid && b.world.bodyCount === 50));
    kept = null;
    std.gc();

    const world = new World({ autoStep: false });
    const bodies = [];
    for (let i = 0; i < 300; i++) bodies.push(world.add({ x: i, y: 0, w: 1, h: 1 }));
    for (let i = 0; i < 300; i += 2) bodies[i].remove();
    std.gc();
    check("removals", world.bodyCount === 150 && world.bodies().every(b => b.valid));
    return world;
}

function debugDraw() {
    const world = new World({ autoStep: false });
    level(world, ["....", "#-/#", "####"]);
    world.add({ x: 10, y: 5, w: 10, h: 10 });
    world.add({ type: "dynamic", x: 40, y: 5, r: 4 });
    if (host) __nativeDraws();
    world.drawDebug();
    if (host) {
        const drawn = __nativeDraws();
        check("debug lines and circles", drawn.lines > 8 && drawn.circles === 1);
        world.drawDebug({ tiles: false, bodies: false });
        check("debug off", __nativeDraws().lines === 0);
    }
    throws("debug options", () => world.drawDebug(3), TypeError);
}

function fastWalls() {
    // A grid with a 45 degree slope: a sweep may climb |dx| px; at 20 px per
    // step that was more than a tile, and a fast body climbed walls.
    for (const dt of [1 / 60, 1 / 30]) {
        for (const speed of [600, 1200]) {
            const world = new World({ autoStep: false, gravity: { x: 0, y: 900 } });
            level(world, ["..........", "..........", "/.....#...", "##########"]);
            const p = world.add({ type: "dynamic", x: 20, y: 34, w: 10, h: 14 });
            for (let i = 0; i < 60; i++) { p.vx = speed; world.step(dt); }
            check(`speed ${speed} dt ${dt.toFixed(3)}: stops at the wall`,
                near(p.right, 96) && near(p.bottom, 48) && p.onWall === 1);
        }
    }
}

function kinematics() {
    const world = new World({ autoStep: false, gravity: { x: 0, y: 900 } });
    world.add({ x: -500, y: 100, w: 2000, h: 10 });
    world.add({ x: -40, y: 0, w: 10, h: 100 });
    const door = world.add({ type: "kinematic", x: 60, y: 40, w: 20, h: 60 });
    const player = world.add({ type: "dynamic", x: 30, y: 86, w: 10, h: 14 });
    const lift = world.add({ type: "kinematic", x: 300, y: 80, w: 40, h: 8 });
    const rider = world.add({ type: "dynamic", x: 310, y: 60, w: 10, h: 20 });
    for (let i = 0; i < 10; i++) world.step(DT);
    check("rider on the lift", rider.ground === lift && player.onGround && !player.crushed);

    // Setting x/y of a kinematic body (as a tween does) carries its riders.
    const x0 = rider.x;
    for (let i = 0; i < 30; i++) {
        lift.x += 2;
        lift.y -= 1;
        world.step(DT);
    }
    check("x/y setters carry the rider", near(rider.x - x0, 60) && near(rider.bottom, lift.y) &&
        rider.ground === lift);
    // setPosition() teleports: the rider stays behind.
    lift.setPosition(lift.x + 100, lift.y);
    world.step(DT);
    check("setPosition does not carry", near(rider.x - x0, 60) && rider.ground !== lift);

    // A door sliding into the player pushes it, and crushes it against the wall.
    door.vx = -120;
    let passedThrough = false, crushedAt = -1;
    for (let i = 0; i < 120 && crushedAt < 0; i++) {
        world.step(DT);
        if (player.crushed) crushedAt = i;
        else if (player.right > door.x + 0.01) passedThrough = true;
    }
    check("pushed, never passed through", !passedThrough);
    check("crushed against the wall", crushedAt > 0 && player.crushed && near(player.x, -30));
    door.vx = 0;
    world.step(DT);
    check("crushed clears on the next step", !player.crushed);

    // A lift rising into a ceiling crushes its rider (it used to fall through the lift).
    const shaft = new World({ autoStep: false, gravity: { x: 0, y: 900 } });
    shaft.add({ x: 0, y: 0, w: 100, h: 10 });
    const elevator = shaft.add({ type: "kinematic", x: 20, y: 60, w: 40, h: 8 });
    const man = shaft.add({ type: "dynamic", x: 30, y: 40, w: 10, h: 20 });
    for (let i = 0; i < 10; i++) shaft.step(DT);
    check("rides the elevator", man.ground === elevator);
    elevator.vy = -120;
    let squashed = false;
    for (let i = 0; i < 40 && !squashed; i++) {
        shaft.step(DT);
        squashed = man.crushed;
    }
    // At the ceiling (y 10); gravity pulls it down in the same step.
    check("rider crushed against the ceiling", squashed && near(man.y, 10, 0.5));
}

function sensors() {
    const world = new World({ autoStep: false });
    const COIN = 8;
    const coins = [0, 1, 2].map(i => world.add({ x: 50 + i * 30, y: 10, r: 5, sensor: true, layer: COIN }));
    const zone = world.add({ x: 200, y: 0, w: 40, h: 40, sensor: true });
    const p = world.add({ type: "dynamic", x: 0, y: 5, w: 10, h: 10, vx: 600 });
    const entered = [], exited = [];
    let picked = 0;

    check("no handlers by default", world.onEnter === null && world.onExit === null);
    throws("onEnter type", () => { world.onEnter = 3; }, TypeError);
    world.onEnter = function (sensor, other) {
        entered.push(sensor);
        check("onEnter this and other", this === world && other === p);
        if (sensor.layer === COIN) { sensor.remove(); picked++; }
    };
    world.onExit = (sensor, other) => exited.push([sensor, other]);
    for (let i = 0; i < 30; i++) world.step(DT);
    check("coins picked once each", picked === 3 && coins.every(c => !c.valid));
    check("zone entered and left", entered.filter(s => s === zone).length === 1 &&
        exited.length === 1 && exited[0][0] === zone && exited[0][1] === p);
    world.onEnter = null;
    world.onExit = null;
    check("handlers cleared", world.onEnter === null);
}

function conveniences() {
    const world = new World({ autoStep: false });
    level(world, ["....", "#-/.", "####"]);
    const box = world.add({ type: "dynamic", x: 100, y: 0, w: 10, h: 20 });
    const ball = world.add({ x: 200, y: 50, r: 5 });
    check("edges and center", box.centerX === 105 && box.centerY === 10 && box.right === 110 &&
        box.bottom === 20 && ball.centerX === 200 && ball.bottom === 55);
    check("solidAt", world.solidAt(8, 24) && !world.solidAt(24, 20) && world.solidAt(44, 26) &&
        !world.solidAt(36, 26) && world.solidAt(105, 5) && !world.solidAt(105, 5, 4));
    throws("solidAt NaN", () => world.solidAt(NaN, 0), RangeError);

    // move() into a reused result object.
    const out = {};
    const r1 = world.move(box, 5, 0, out);
    const contacts = out.contacts;
    const r2 = box.move(-500, 0, out);
    check("move out", r1 === out && r2 === out && out.contacts === contacts &&
        out.hitWall === -1 && out.contacts.length === 1 && out.contacts[0].tile === 3 &&
        near(box.x, 48));   // the high side of the slope is a wall
    box.move(1, 0, out);
    check("move out reset", out.hitWall === 0 && out.contacts.length === 0 && out.contacts === contacts);
    throws("move out type", () => box.move(1, 0, 5), TypeError);

    // pairs() with a callback, with a nested call and a removal.
    const pw = new World({ autoStep: false });
    const enemies = [0, 1, 2].map(i => pw.add({ x: i * 20, y: 0, w: 10, h: 10, layer: 2 }));
    const bullets = [0, 1, 2].map(i => pw.add({ x: i * 20 + 2, y: 2, r: 2, layer: 4 }));
    let calls = 0, nested = 0;
    const count = pw.pairs(4, 2, (bullet, enemy) => {
        calls++;
        nested += pw.pairs(4, 2).length;
        check("pair order", bullets.includes(bullet) && enemies.includes(enemy));
        if (calls === 1) bullets[1].remove();
    });
    check("pairs callback", count === 3 && calls === 2 && nested === 5);
    throws("pairs callback type", () => pw.pairs(4, 2, 5), TypeError);
    for (let i = 0; i < 400; i++) pw.add({ x: 500, y: 500, w: 4, h: 4, layer: 16 });
    throws("too many pairs", () => pw.pairs(16, 16), RangeError);

    throws("grid too large", () => world.setGrid({ columns: 4096, rows: 4096, tileWidth: 8,
        tileHeight: 8 }), RangeError);
}

async function main() {
    basics();
    movement();
    tiles();
    physics();
    contacts();
    queries();
    raycasts();
    shapes();
    debugDraw();
    fastWalls();
    kinematics();
    sensors();
    conveniences();
    const stepped = await loopSystem();
    const kept = garbage();
    // Left alive until the runtime ends: teardown must free them.
    globalThis.__keep = { stepped, kept };

    console.log(`Result: ${passed} passed, ${failed} failed` + (host ? "" : " (host-only checks skipped)"));
    if (!failed) console.log("Collision module test passed");
}

main().catch(error => {
    console.log("[FAIL] " + error + "\n" + (error && error.stack));
});
