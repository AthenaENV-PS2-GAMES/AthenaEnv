// Collision example: a small platformer on a tile grid, stepped by the Loop.
//
//   D-pad LEFT / RIGHT   walk              CROSS      jump
//   D-pad DOWN           drop through one-way platforms (orange)
//   SQUARE               throw a bouncing ball
//   TRIANGLE             debug outlines on / off
//   START                quit
//
// The level has solid tiles, one-way platforms and a hill of slopes; a
// kinematic platform carries the player; coins are sensors picked up in
// world.onEnter; a raycast from the player shows what it looks at. The
// camera follows the player: every Draw call is in world coordinates.

const TILE = 16;
const LEVEL = [
    "#..............................................................#",
    "#..............................................................#",
    "#..............................................................#",
    "#.......c.c.c..................................c...............#",
    "#.....---------.............................-------............#",
    "#..............................................................#",
    "#.............................c.c..............................#",
    "#...........................#######..............c.c.c.........#",
    "#.................c...........................---------........#",
    "#..........................................................c...#",
    "#........................................................./#\\..#",
    "#...c.c.........../#\\..................................../###\\.#",
    "#............../#####\\........######..................../#####\\#",
    "#............./#######\\.......######...................#########",
    "################################################################",
];
const COLUMNS = LEVEL[0].length, ROWS = LEVEL.length;
const SOLID = 1, PLAYER = 2, COIN = 4, BALL = 8;
const ID = { "#": 1, "-": 2, "/": 3, "\\": 4 };

const rgb = (r, g, b, a = 128) => Color.new(r, g, b, a);
const SKY = rgb(18, 22, 40), GROUND = rgb(70, 90, 130), LEDGE = rgb(230, 150, 60);
const HERO = rgb(255, 170, 40), GOLD = rgb(255, 220, 60), RAY = rgb(120, 255, 160, 90);
const LIFT = rgb(90, 140, 255), BALL_COLOR = rgb(240, 90, 90);

const tiles = new Uint16Array(COLUMNS * ROWS);
const coins = [];
const world = new Collision.World({ gravity: { x: 0, y: 1100 } });
LEVEL.forEach((row, r) => [...row].forEach((ch, c) => {
    tiles[r * COLUMNS + c] = ID[ch] || 0;
    if (ch === "c")
        coins.push(world.add({ x: c * TILE + 8, y: r * TILE + 8, r: 5, sensor: true, layer: COIN }));
}));
world.setGrid({
    columns: COLUMNS, rows: ROWS, tileWidth: TILE, tileHeight: TILE, tiles,
    solid: [1], oneWay: [2], slopes: { 3: "45r", 4: "45l" }, layer: SOLID,
});

const player = world.add({ type: "dynamic", x: 40, y: 180, w: 12, h: 20,
    layer: PLAYER, mask: SOLID | COIN, maxSpeedY: 600 });
const lift = world.add({ type: "kinematic", x: 400, y: 150, w: 48, h: 8, vx: 50, layer: SOLID });
const balls = [];

let debug = false, facing = 1, score = 0;
world.onEnter = sensor => {
    if (sensor.layer !== COIN) return;
    sensor.remove();
    score++;
};
const font = new Font();
const pad = Gamepad.player(0);
const cam = Camera2D.main;
cam.setBounds(0, 0, COLUMNS * TILE, ROWS * TILE);
cam.follow(player, { lerp: 8, deadzone: { w: 40, h: 30 }, lookahead: 30 });
cam.zoom = 2;

function drawLevel() {
    const view = cam.visibleRect();
    const c0 = Math.max(0, Math.floor(view.x / TILE)), c1 = Math.min(COLUMNS - 1, Math.floor((view.x + view.w) / TILE));
    const r0 = Math.max(0, Math.floor(view.y / TILE)), r1 = Math.min(ROWS - 1, Math.floor((view.y + view.h) / TILE));
    for (let r = r0; r <= r1; r++) {
        for (let c = c0; c <= c1; c++) {
            const id = tiles[r * COLUMNS + c], x = c * TILE, y = r * TILE;
            if (id === 1) Draw.rect(x, y, TILE, TILE, GROUND);
            else if (id === 2) Draw.rect(x, y, TILE, 4, LEDGE);
            else if (id === 3) Draw.triangle(x, y + TILE, x + TILE, y + TILE, x + TILE, y, GROUND);
            else if (id === 4) Draw.triangle(x, y, x, y + TILE, x + TILE, y + TILE, GROUND);
        }
    }
}

Loop.run({
    update() {
        Gamepad.update();
        if (pad.justPressed(Gamepad.START)) { Loop.stop(); return; }
        if (pad.justPressed(Gamepad.TRIANGLE)) debug = !debug;

        player.vx = 0;
        if (pad.pressed(Gamepad.LEFT)) { player.vx = -110; facing = -1; }
        if (pad.pressed(Gamepad.RIGHT)) { player.vx = 110; facing = 1; }
        if (pad.justPressed(Gamepad.CROSS) && player.onGround) player.vy = -340;
        player.dropThrough = pad.pressed(Gamepad.DOWN);

        if (pad.justPressed(Gamepad.SQUARE)) {
            balls.push(world.add({ type: "dynamic", x: (player.x + player.w / 2) + facing * 10, y: player.y + 4, r: 4,
                vx: facing * 160, vy: -200, bounce: 0.7, damping: 0.3, layer: BALL, mask: SOLID | BALL }));
            if (balls.length > 12) balls.shift().remove();
        }
        // The lift turns around at the ends of its track.
        if ((lift.x > 560 && lift.vx > 0) || (lift.x < 400 && lift.vx < 0)) lift.vx = -lift.vx;

    },
    draw() {
        Screen.clear(SKY);
        // Camera2D.main applies to every draw: world coordinates.
        drawLevel();
        Draw.rect(lift.x, lift.y, lift.w, lift.h, LIFT);
        for (const coin of coins) if (coin.valid) Draw.circle(coin.x, coin.y, coin.r, GOLD, true);
        for (const ball of balls) Draw.circle(ball.x, ball.y, ball.r, BALL_COLOR, true);
        Draw.rect(player.x, player.y, player.w, player.h, HERO);

        // Line of sight: what the player looks at, up to 160 px ahead.
        const eyeX = player.x + player.w / 2, eyeY = player.y + 6;
        const hit = world.raycast(eyeX, eyeY, eyeX + facing * 160, eyeY, { ignore: player, mask: SOLID });
        Draw.line(eyeX, eyeY, hit ? hit.x : eyeX + facing * 160, eyeY, RAY);
        if (hit) Draw.circle(hit.x, hit.y, 2, RAY, true);

        if (debug) world.drawDebug();
        Camera2D.screenSpace(() => font.print(10, 10, `coins ${score}/${coins.length}   ${player.onGround ? "ground" : "air"}` +
            `   TRIANGLE: debug ${debug ? "on" : "off"}`));
    },
});
