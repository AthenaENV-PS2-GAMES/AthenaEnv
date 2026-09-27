// Sprite example: an animated hero followed by the camera, and a field of
// coins animated in C on a TileMap by Sprite.Animator.
//
//   Left stick / D-pad   walk (run clip, flips to face the direction)
//   CROSS                attack (plays once, then back to idle)
//   L1 / R1              slower / faster animations
//   SQUARE               pause / resume the coins
//   SELECT               debug overlay on / off (outlines, origins)
//   START                quit
//
// texture.png (256 x 96) is cut in 32 x 32 cells: the clips below only show
// how clips, events and the animator work, not a real character.

const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
const CELL = 32;
const COINS = 300;
const WORLD_W = 1600, WORLD_H = 900;
const SPEED = 160;

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try { return new Image(path); } catch (error) { /* next */ }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", "));
}

const atlas = loadAtlas();
const sheet = Sprite.Sheet.fromGrid(atlas, {
    frameWidth: CELL, frameHeight: CELL,
    clips: {
        idle:   { frames: "0-3", fps: 4, mode: "pingpong" },
        run:    { frames: "8-15", fps: 12 },
        // Back to idle by itself when it ends.
        attack: { frames: "16-21", durations: [60, 60, 80, 120, 160, 200], mode: "once", next: "idle" },
        spin:   { frames: "0-7", fps: 10 },
    },
});

// The hero: bottom-center origin, so flipping turns it in place.
const player = { x: WORLD_W / 2, y: WORLD_H / 2, attacking: false };
const hero = new Sprite.Instance(sheet, { clip: "idle", origin: [0.5, 1], scale: 2 });
let steps = 0;
hero.on("run:2", () => steps++);                       // position 2 of the run clip
hero.on("end:attack", () => player.attacking = false);

// Coins: one TileMap buffer of COINS sprites, animated by one Animator.
const rng = new Random.Generator("sprite");
const buffer = TileMap.SpriteBuffer.create(COINS);
const view = new DataView(buffer);
const { stride, offsets } = TileMap.layout;
for (let i = 0; i < COINS; i++) {
    const base = i * stride;
    view.setFloat32(base + offsets.x, rng.int(0, WORLD_W - CELL), true);
    view.setFloat32(base + offsets.y, rng.int(0, WORLD_H - CELL), true);
    view.setFloat32(base + offsets.w, CELL, true);
    view.setFloat32(base + offsets.h, CELL, true);
    for (const channel of ["r", "g", "b", "a"]) view.setUint32(base + offsets[channel], 128, true);
}
const coins = new TileMap.Instance({
    descriptor: new TileMap.Descriptor({ textures: [atlas], materials: [{ endOffset: COINS - 1 }] }),
    spriteBuffer: buffer,
});
const coinAnimator = Sprite.Animator.bind(coins, sheet, "spin", { randomStart: true });

const cam = Camera2D.main;
cam.setBounds(0, 0, WORLD_W, WORLD_H);
cam.follow(player, { lerp: 8, deadzone: { w: 48, h: 32 } });

const font = new Font();
const pad = Gamepad.player(0);
let speed = 1;
let debug = false;

Loop.run({
    update(dt) {
        Gamepad.update();
        if (pad.justPressed(Gamepad.START)) return Loop.stop();
        const d = pad.dpad();
        const dx = d.x || pad.leftX, dy = d.y || pad.leftY;

        if (pad.justPressed(Gamepad.CROSS) && !player.attacking) {
            player.attacking = true;
            hero.play("attack");
        }
        if (!player.attacking) {
            player.x = Math.min(WORLD_W, Math.max(0, player.x + dx * SPEED * dt));
            player.y = Math.min(WORLD_H, Math.max(CELL, player.y + dy * SPEED * dt));
            // play() of the clip already playing does nothing: call it every frame.
            hero.play(dx || dy ? "run" : "idle");
            if (dx) hero.flipX = dx < 0;
        }
        if (pad.justPressed(Gamepad.L1)) speed = Math.max(0.25, speed / 2);
        if (pad.justPressed(Gamepad.R1)) speed = Math.min(4, speed * 2);
        hero.speed = speed;
        coinAnimator.speed = speed;
        if (pad.justPressed(Gamepad.SQUARE)) coinAnimator.paused ? coinAnimator.resume() : coinAnimator.pause();
        if (pad.justPressed(Gamepad.SELECT)) Sprite.setDebug(debug = !debug);
    },
    draw() {
        coins.render(0, 0);
        hero.draw(player.x, player.y);
        Camera2D.screenSpace(() => {
            const stats = Sprite.getStats();
            font.print(24, 20, `clip ${hero.clip} frame ${hero.frame}  steps ${steps}  speed x${speed}`);
            font.print(24, 40, `${stats.animatedSprites} coins on the Animator` +
                `${coinAnimator.paused ? " (paused)" : ""}  ${Loop.getStats().fps.toFixed(0)} FPS`);
        });
    },
}, { clearColor: Color.new(24, 28, 40, 128) });
