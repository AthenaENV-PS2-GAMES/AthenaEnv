import * as Screen from "Screen";

function assertEqual(name, actual, expected) {
    if (actual !== expected) {
        throw new Error(`${name}: expected ${expected}, got ${actual}`);
    }
}

const mode = Screen.getMode();
if (mode.width <= 0 || mode.height <= 0) {
    throw new Error("Screen.getMode returned invalid dimensions");
}

Screen.setVSync(true);
Screen.setFrameCounter(false);
Screen.clear(0x80000000);
Screen.flush();
Screen.waitVblankStart();
Screen.flip();

Screen.setParam(Screen.DEPTH_TEST_ENABLE, false);
assertEqual("depth parameter disabled",
    Screen.getParam(Screen.DEPTH_TEST_ENABLE), false);
Screen.setParam(Screen.DEPTH_TEST_ENABLE, true);
assertEqual("depth parameter enabled",
    Screen.getParam(Screen.DEPTH_TEST_ENABLE), true);

const alpha = Screen.getParam(Screen.ALPHA_BLEND_EQUATION);
if (!alpha || typeof alpha.a !== "number" || typeof alpha.fix !== "number") {
    throw new Error("Screen alpha equation is invalid");
}

console.log("Screen module test passed");
