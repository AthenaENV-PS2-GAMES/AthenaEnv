// Camera2D example: a TileMap world, followed by one camera or split in two.
//
//   Left stick / D-pad   move player 1        Right stick   move player 2
//   L1 / R1              zoom out / in         L2 / R2       turn the camera
//   CROSS                shake                 CIRCLE        flash
//   SQUARE               letterbox on / off    TRIANGLE      fade out and back in
//   SELECT (tap)         split screen on / off R3            rooms (zones) on / off
//   SELECT + SQUARE      bounds ignore the rotation on / off
//   L3                   reset zoom and turn   START         quit
//
// SELECT is also a modifier: held with another button it does that
// button's combination instead, and does not toggle split screen.
//
// Everything is drawn in world coordinates: the camera is applied in C to
// the TileMap, Draw and Font calls of draw(). The HUD uses screenSpace()
// (or viewportSpace() per player in split screen).

const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
const TILE = 16;
const COLUMNS = 128, ROWS = 64;               // 2048 x 1024 world
const WORLD_W = COLUMNS * TILE, WORLD_H = ROWS * TILE;
const SPEED = 180;                            // world units per second

const rgb = (r, g, b, a = 128) => Color.new(r, g, b, a);
const WHITE = rgb(255, 255, 255), RED = rgb(230, 60, 60), BLUE = rgb(60, 120, 240);
const SKY = rgb(20, 24, 48), STAR = rgb(200, 200, 255, 90), ROOM = rgb(255, 220, 0, 60);

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try { return new Image(path); } catch (error) { /* next */ }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", "));
}

const atlasImage = loadAtlas();
const ATLAS_COLS = Math.floor(atlasImage.width / TILE);
const ATLAS_ROWS = Math.floor(atlasImage.height / TILE);
const rng = new Random.Generator("camera2d");
const tiles = new Uint16Array(COLUMNS * ROWS);
for (let i = 0; i < tiles.length; i++) tiles[i] = rng.int(0, ATLAS_COLS * ATLAS_ROWS - 1);
const level = TileMap.Instance.fromGrid({
    descriptor: new TileMap.Descriptor({
        textures: [atlasImage],
        materials: [{ endOffset: COLUMNS * ROWS - 1 }],
        atlas: { tileWidth: TILE, tileHeight: TILE, columns: ATLAS_COLS, rows: ATLAS_ROWS },
    }),
    columns: COLUMNS, rows: ROWS, tiles,
});

// A far layer of "stars": drawn with parallax 0.3, it scrolls slower.
const stars = [];
for (let i = 0; i < 120; i++) stars.push({ x: rng.int(0, 1400), y: rng.int(0, 900), r: rng.int(1, 3) });

const font = new Font();
const pads = [Gamepad.player(0), Gamepad.player(1)];
const players = [
    { x: 700, y: 500, color: RED },
    { x: 1700, y: 500, color: BLUE },
];
const rooms = [
    { x: 0, y: 0, w: WORLD_W / 2, h: WORLD_H },
    { x: WORLD_W / 2, y: 0, w: WORLD_W / 2, h: WORLD_H, zoom: 1.5 },
];

const { width: SCREEN_W, height: SCREEN_H } = Screen.getMode();
const main = Camera2D.main;
main.setBounds(0, 0, WORLD_W, WORLD_H);
// The dead zone and the lookahead are screen pixels: the player may rest up
// to lookahead + half the dead zone off the center (here 24 + 16 px), at any
// zoom and rotation. Near the map's edges the bounds push the camera inward,
// more so when it is turned (a turned view covers more of the world).
main.follow(players[0], { lerp: 8, deadzone: { w: 32, h: 24 }, lookahead: 24 });

// Split screen: one camera per half, following one player each.
const halves = [0, 1].map(i => {
    const cam = new Camera2D.Camera({
        viewport: { x: i * SCREEN_W / 2, y: 0, w: SCREEN_W / 2, h: SCREEN_H },
        bounds: { x: 0, y: 0, w: WORLD_W, h: WORLD_H },
    });
    return cam.follow(players[i], { lerp: 6 });
});

const state = { split: false, zones: false, letterbox: false, zone: -1, selectUsed: false };

// Title-safe area: CRT TVs hide about 5% of each edge.
const SAFE_X = Math.round(SCREEN_W * 0.05), SAFE_Y = Math.round(SCREEN_H * 0.05);
const LINE_H = font.getTextSize("Ag").height + 4;

// The controls, wrapped once into lines that fit the safe area.
const HELP = ["L1/R1 zoom", "L2/R2 turn", "L3 reset", "X shake", "O flash", "[] bars",
    "/\\ fade", "SELECT split", "SELECT+[] bounds", "R3 rooms", "START quit"];
function wrap(items, width) {
    const lines = [];
    let line = "";
    for (const item of items) {
        const next = line ? line + "   " + item : item;
        if (line && font.getTextSize(next).width > width) {
            lines.push(line);
            line = item;
        } else {
            line = next;
        }
    }
    if (line) lines.push(line);
    return lines;
}
const helpLines = wrap(HELP, SCREEN_W - 2 * SAFE_X);

function toggleBoundsRotation() {
    const on = !main.boundsIgnoreRotation;
    for (const cam of [main, ...halves]) cam.boundsIgnoreRotation = on;
}

function toggleSplit() {
    state.split = !state.split;
    // Without a current camera nothing is applied around draw(): each half
    // is drawn with its own camera.draw().
    Camera2D.setCurrent(state.split ? null : main);
}

function toggleZones() {
    state.zones = !state.zones;
    main.setZones(state.zones ? rooms : null, {
        transition: 0.4,
        onChange(zone) { state.zone = zone; this.flash(rgb(255, 255, 255, 60), 0.15); },
    });
    if (!state.zones) main.zoomTo(1, 0.3);
}

async function fadeOutIn() {
    await main.fade(rgb(0, 0, 0, 128), 0.4);
    await main.fade(rgb(0, 0, 0, 0), 0.4);
}

function move(player, pad, dx, dy, dt) {
    player.x = Math.max(0, Math.min(WORLD_W, player.x + dx * SPEED * dt));
    player.y = Math.max(0, Math.min(WORLD_H, player.y + dy * SPEED * dt));
}

function drawWorld(cam) {
    // Background layer, slower than the world.
    cam.draw(() => {
        for (const s of stars) Draw.rect(s.x, s.y, s.r, s.r, STAR);
    }, { parallax: 0.3 });
    level.render(0, 0);
    if (state.zones) for (const room of rooms) {
        Draw.line(room.x, 0, room.x, WORLD_H, ROOM);
    }
    for (const p of players) {
        Draw.rect(p.x - 6, p.y - 6, 12, 12, p.color);
        Draw.circle(p.x, p.y, 20, p.color, false);
    }
    font.print(players[0].x - 20, players[0].y - 40, "P1");
}

// Inside a viewport (split screen) only the part of the safe margin that
// falls on it applies: each half touches one side edge.
function hud(cam, label) {
    const vp = cam.viewport;
    const left = Math.max(0, SAFE_X - vp.x) + 4, top = Math.max(0, SAFE_Y - vp.y);
    font.print(left, top, `${label}  x ${cam.x | 0}  y ${cam.y | 0}  zoom ${cam.zoom.toFixed(2)}`);
    font.print(left, top + LINE_H, `${Loop.getStats().fps.toFixed(0)} FPS` +
        (state.zones ? `  room ${state.zone}` : "") +
        (cam.boundsIgnoreRotation ? "  bounds: unturned" : "  bounds: box"));
}

function drawHelp() {
    const top = SCREEN_H - SAFE_Y - helpLines.length * LINE_H;
    helpLines.forEach((line, i) => font.print(SAFE_X, top + i * LINE_H, line));
}

Loop.run({
    update(dt) {
        Gamepad.update();
        const [p1, p2] = pads;
        if (p1.justPressed(Gamepad.START)) return Loop.stop();
        const d = p1.dpad();
        move(players[0], p1, d.x || p1.leftX, d.y || p1.leftY, dt);
        move(players[1], p1, p1.rightX, p1.rightY, dt);
        if (p2.connected) move(players[1], p2, p2.leftX, p2.leftY, dt);

        // SELECT held is a modifier; tapped alone it toggles split screen.
        const modifier = p1.pressed(Gamepad.SELECT);
        if (p1.justPressed(Gamepad.SELECT)) state.selectUsed = false;
        if (modifier && p1.justPressed(Gamepad.SQUARE)) {
            toggleBoundsRotation();
            state.selectUsed = true;
        }
        if (p1.justReleased(Gamepad.SELECT) && !state.selectUsed) toggleSplit();

        const cam = state.split ? halves[0] : main;
        if (p1.justPressed(Gamepad.L1)) cam.zoomTo(Math.max(0.25, cam.zoom / 1.5), 0.25);
        if (p1.justPressed(Gamepad.R1)) cam.zoomTo(Math.min(4, cam.zoom * 1.5), 0.25);
        if (p1.pressed(Gamepad.L2)) cam.rotation -= 1.5 * dt;
        if (p1.pressed(Gamepad.R2)) cam.rotation += 1.5 * dt;
        if (p1.justPressed(Gamepad.L3)) { cam.rotation = 0; cam.zoomTo(1, 0.25); }
        if (p1.justPressed(Gamepad.CROSS)) cam.shake(8, 0.4, { rotation: 0.03 });
        if (p1.justPressed(Gamepad.CIRCLE)) cam.flash(rgb(255, 255, 255, 100), 0.2);
        if (!modifier && p1.justPressed(Gamepad.SQUARE)) {
            state.letterbox = !state.letterbox;
            cam.letterbox(state.letterbox ? 0.12 : 0, 0.3);
        }
        if (p1.justPressed(Gamepad.TRIANGLE)) fadeOutIn();
        if (p1.justPressed(Gamepad.R3)) toggleZones();
    },
    draw() {
        if (state.split) {
            halves.forEach((cam, i) => {
                cam.draw(() => drawWorld(cam));
                cam.viewportSpace(() => hud(cam, "P" + (i + 1)));
            });
        } else {
            // main is current: this draw is already in its world space.
            drawWorld(main);
            Camera2D.screenSpace(() => hud(main, "main"));
        }
        Camera2D.screenSpace(drawHelp);
    },
}, { clearColor: SKY });
