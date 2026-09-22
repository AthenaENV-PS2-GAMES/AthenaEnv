import * as Color from "Color";
import * as Draw from "Draw";
import * as Screen from "Screen";

function assert(condition, message) {
    if (!condition)
        throw new Error(message);
}

const red = Color.new(255, 0, 0);
const green = Color.new(0, 255, 0);
const blue = Color.new(0, 0, 255);
const white = Color.new(255, 255, 255);

assert(typeof Draw.point === "function", "Draw.point must be available");
assert(typeof Draw.line === "function", "Draw.line must be available");
assert(typeof Draw.triangle === "function",
    "Draw.triangle must be available");
assert(typeof Draw.triangleGouraud === "function",
    "Draw.triangleGouraud must be available");
assert(typeof Draw.quad === "function", "Draw.quad must be available");
assert(typeof Draw.quadGouraud === "function",
    "Draw.quadGouraud must be available");
assert(typeof Draw.rect === "function", "Draw.rect must be available");
assert(typeof Draw.circle === "function", "Draw.circle must be available");

while (true) {
    Screen.clear(Color.new(24, 24, 32));

    Draw.point(32, 32, white);
    Draw.line(48, 32, 112, 32, red);
    Draw.triangle(144, 32, 112, 88, 176, 88, red);
    Draw.triangleGouraud(
        224, 32, red,
        192, 88, green,
        256, 88, blue
    );
    Draw.quad(288, 32, 352, 32, 352, 88, 288, 88, green);
    Draw.quadGouraud(
        384, 32, red,
        448, 32, green,
        448, 88, blue,
        384, 88, white
    );
    Draw.rect(48, 120, 64, 40, blue);
    Draw.circle(176, 140, 28, white);
    Draw.circle(256, 140, 28, red, false);

    Screen.flip();
}

print("draw_test passed");
