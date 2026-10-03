// Debug example: overlay, watches, console and hitboxes over a small scene.
//
//   L3 + R3   show / hide all debug drawing (read without Gamepad.update)
//   CROSS     spawn 50 balls (watch the CPU bar grow)
//   CIRCLE    remove the balls
//   SQUARE    toggle the console
//   TRIANGLE  toggle the hitboxes of every ball
//   R1        toggle a compact overlay (FPS line and watches: cheaper)
//   START     quit
//
// Balls that hit a wall leave a red box for half a second and log a line.
// The hitboxes of all the balls go to Debug.rects() in one call per frame,
// from a Float32Array reused between frames: one Debug.rect() per ball would
// cost about 50 microseconds each on the PS2, mostly calling into C.

const WIDTH = 640, HEIGHT = 448, SIZE = 12;
const BALL = Color.new(255, 180, 0);
const HIT = Color.new(255, 40, 40);
const BOX = Color.new(80, 220, 120);

const pad = Gamepad.player(0);
const rng = new Random.Generator();
const balls = [];
let bounces = 0;
let hitboxes = true, compact = false;
let boxes = new Float32Array(0);   // x, y, width, height per ball

function drawHitboxes() {
    if (boxes.length < balls.length * 4) boxes = new Float32Array(balls.length * 8);
    for (let i = 0; i < balls.length; i++) {
        const o = i * 4;
        boxes[o] = balls[i].x;
        boxes[o + 1] = balls[i].y;
        boxes[o + 2] = SIZE;
        boxes[o + 3] = SIZE;
    }
    Debug.rects(boxes.subarray(0, balls.length * 4), BOX);
}

function spawn(count) {
    for (let i = 0; i < count; i++) {
        const angle = rng.angle(), speed = rng.float(60, 180);
        balls.push({ x: rng.float(40, WIDTH - 40), y: rng.float(40, HEIGHT - 40),
            vx: Math.cos(angle) * speed, vy: Math.sin(angle) * speed });
    }
    console.log(`spawned ${count}: ${balls.length} balls`);
}

Debug.overlay(true);
Debug.console(true, { lines: 5 });
Debug.toggleWith(Gamepad.L3 | Gamepad.R3);
Debug.watch("balls", () => balls.length);
Debug.watch("bounces", () => bounces);
Debug.watch("first", () => balls.length ? `${balls[0].x | 0},${balls[0].y | 0}` : "-");

spawn(20);

Loop.run({
    update(dt) {
        for (const ball of balls) {
            ball.x += ball.vx * dt;
            ball.y += ball.vy * dt;
            let hit = false;
            if (ball.x < 0 || ball.x > WIDTH - SIZE) { ball.vx = -ball.vx; hit = true; }
            if (ball.y < 0 || ball.y > HEIGHT - SIZE) { ball.vy = -ball.vy; hit = true; }
            if (hit) {
                bounces++;
                Debug.rect(ball.x - 2, ball.y - 2, SIZE + 4, SIZE + 4, HIT, { seconds: 0.5 });
                if (bounces % 25 === 0) console.log(`bounce #${bounces}`);
            }
        }
    },
    draw() {
        Gamepad.update();
        if (pad.justPressed(Gamepad.START)) return Loop.stop();
        if (pad.justPressed(Gamepad.CROSS)) spawn(50);
        if (pad.justPressed(Gamepad.CIRCLE)) { balls.length = 0; console.log("cleared"); }
        if (pad.justPressed(Gamepad.SQUARE)) Debug.console(!Debug.console());
        if (pad.justPressed(Gamepad.TRIANGLE)) hitboxes = !hitboxes;
        if (pad.justPressed(Gamepad.R1)) { compact = !compact; Debug.overlay(true, { compact }); }

        for (const ball of balls) Draw.rect(ball.x, ball.y, SIZE, SIZE, BALL);
        if (hitboxes) drawHitboxes();
    },
}, { clearColor: Color.new(20, 20, 36) });
