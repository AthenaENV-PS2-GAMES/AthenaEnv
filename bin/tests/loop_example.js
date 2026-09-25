// Loop example: fixed-step movement drawn with interpolation.
//
//   Left stick  move the square
//   CROSS       pause / resume
//   CIRCLE      toggle slow motion
//   SQUARE      toggle 60 / 30 FPS
//   START       quit

const STEP = 1 / 60;
const SPEED = 240;          // pixels per second
const SIZE = 32;
const WIDTH = 640, HEIGHT = 448;
const WHITE = Color.new(255, 255, 255);
const BALL = Color.new(255, 180, 0);

const font = new Font();
const pad = Gamepad.player(0);
let paused = false, slow = false, halfRate = false;

// Positions of the previous and the current step, blended by alpha in draw().
const player = { x: WIDTH / 2, y: HEIGHT / 2, px: WIDTH / 2, py: HEIGHT / 2 };
const ball = { x: 100, y: 100, px: 100, py: 100, vx: 180, vy: 140 };

const lerp = (a, b, t) => a + (b - a) * t;

function applyTimeScale() {
    Loop.setTimeScale(paused ? 0 : slow ? 0.25 : 1);
}

const game = {
    update(step) {
        player.px = player.x; player.py = player.y;
        player.x = Math.min(Math.max(player.x + pad.leftX * SPEED * step, 0), WIDTH - SIZE);
        player.y = Math.min(Math.max(player.y + pad.leftY * SPEED * step, 0), HEIGHT - SIZE);

        ball.px = ball.x; ball.py = ball.y;
        ball.x += ball.vx * step;
        ball.y += ball.vy * step;
        if (ball.x < 0 || ball.x > WIDTH - 16) ball.vx = -ball.vx;
        if (ball.y < 0 || ball.y > HEIGHT - 16) ball.vy = -ball.vy;
    },

    // Input lives in draw(): it runs every frame, even while paused.
    draw(alpha) {
        Gamepad.update();
        if (pad.justPressed(Gamepad.START)) return Loop.stop();
        if (pad.justPressed(Gamepad.CROSS)) { paused = !paused; applyTimeScale(); }
        if (pad.justPressed(Gamepad.CIRCLE)) { slow = !slow; applyTimeScale(); }
        if (pad.justPressed(Gamepad.SQUARE)) {
            halfRate = !halfRate;
            Loop.run(game, options());
        }

        Draw.rect(lerp(player.px, player.x, alpha), lerp(player.py, player.y, alpha), SIZE, SIZE, WHITE);
        Draw.rect(lerp(ball.px, ball.x, alpha), lerp(ball.py, ball.y, alpha), 16, 16, BALL);

        const stats = Loop.getStats();
        font.print(10, 8, stats.fps.toFixed(1) + " FPS  cpu " + stats.cpuMs.toFixed(2) +
            " ms  steps " + stats.steps + "  time " + Loop.getElapsedTime().toFixed(1) + " s" +
            (paused ? "  PAUSED" : slow ? "  SLOW" : ""));
    },
};

function options() {
    return { fixedStep: STEP, vsyncInterval: halfRate ? 2 : 1, clearColor: Color.new(20, 20, 40) };
}

Loop.run(game, options());
