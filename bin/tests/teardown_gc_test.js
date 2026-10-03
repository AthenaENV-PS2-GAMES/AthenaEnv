/*
 * Objects left alive when a script ends must not leak: the runtime is torn
 * down after every script, and JS_FreeRuntime aborts on a leak. Each case
 * below leaked because a native object held a JavaScript value it did not
 * report to the cycle collector (gc_mark):
 *
 *   - a FontRender kept in a global or a module holds its Font;
 *   - a Noise.fillAsync() Job kept in a global holds its Float32Array;
 *   - the Debug overlay left on keeps FontRenders in its module state;
 *   - a TileMap Instance kept in a global holds its descriptor and sprite
 *     buffer, and the descriptor its Image textures (console only);
 *   - an ImageList kept in a global holds its Images and onLoad/onError
 *     callbacks, here closing over the list itself (console only).
 *
 * The test passes when the script ends and the runtime is freed without
 * "Object leaks" (host: tests/js/run.sh with runner_font; console: the
 * launcher comes back instead of an assertion).
 */
import * as Loop from "Loop";
import * as Debug from "Debug";
import * as Noise from "Noise";
import { Font } from "Font";

const font = new Font({ size: 16 });
globalThis.title = font.render("kept until the end");
globalThis.renders = [font.render("a"), font.render("b")];
globalThis.moduleFont = font;

// Console-only modules: the host runner has no GS, so no TileMap nor ImageList
// (only stand-ins of Image and TileMap.Instance, for the Sprite binding).
if (typeof TileMap !== "undefined" && typeof TileMap.Descriptor === "function" &&
    typeof Image !== "undefined") {
    const descriptor = new TileMap.Descriptor({
        textures: [new Image("tests/texture.png")],
        materials: [{ endOffset: 8 * 8 - 1 }],
        atlas: { tileWidth: 16, tileHeight: 16, columns: 2, rows: 2 },
    });
    globalThis.map = TileMap.Instance.fromGrid({ descriptor, columns: 8, rows: 8 });
    console.log("TileMap instance kept in a global");
}
if (typeof ImageList !== "undefined") {
    const list = new ImageList();
    globalThis.list = list;
    // Callbacks closing over the list: a cycle through C memory.
    list.load("tests/my_image.png", { onLoad: () => list.stats(), onError: () => list.stats() });
    list.load("tests/texture.png", { onLoad: () => list.stats() });
    console.log("ImageList with pending loads kept in a global");
}

globalThis.doneJob = Noise.fillAsync(new Float32Array(64), 8, 8);
globalThis.doneJob.then(() => {
    // Still running when the script ends: holds its array meanwhile.
    globalThis.pendingJob = Noise.fillAsync(new Float32Array(128 * 128), 128, 128, { octaves: 16 });
    Debug.overlay(true);
    Debug.console(true);
    let frames = 0;
    Loop.run({
        draw() {
            if (++frames === 3) {
                Loop.stop();   // the overlay, console and their FontRenders stay on
                console.log("Teardown GC test done: the runtime must now be freed without leaks");
            }
        },
    });
});
