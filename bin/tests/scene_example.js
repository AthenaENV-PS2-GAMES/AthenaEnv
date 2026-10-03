// Scene example: a title, a level and a pause menu, with their assets
// loaded in the background and released when they are no longer used.
//
//   Title:  CROSS   go to the level (fade)
//   Level:  D-pad   move            CROSS    play the "pop" sound
//           START   pause menu      SELECT   back to the title (wipe)
//   Pause:  START   resume          CIRCLE   quit to the title
//
// The HUD shows the scene stack and what Scene.Assets holds: the level's
// sheet and sounds appear as soon as the title preloads them, and stay while
// you go back and forth (the title preloads the level again each time).
// The level loads slowly on purpose (a 1 s extra asset). The title preloads
// it (Scene.preload), so after waiting a second on the title the level
// enters at once; press CROSS right away to see the loading screen instead.

const pad = Gamepad.player(0);
const WHITE = Color.new(255, 255, 255), GREY = Color.new(150, 150, 150);

// Paths are tried from bin/ and from bin/tests/.
const ROOT = (() => {
    for (const root of ["tests", ".", "bin/tests"]) {
        try { std.loadFile(`${root}/sprite_sheet.json`).length; return root; } catch (e) { /* next */ }
    }
    return "tests";
})();

// A custom kind: "loaded" after `seconds`, to see the loading screen. A Loop
// system counts the frames' real time down (a promise loop would hold the
// frame, and Date.now() has no precision left in float32 numbers).
const delays = [];
Loop.addSystem({
    name: "delays",
    realTime: true,
    preUpdate(realDt) {
        for (let i = delays.length - 1; i >= 0; i--) {
            delays[i].left -= realDt;
            if (delays[i].left <= 0) {
                delays[i].resolve({ path: delays[i].path });
                delays.splice(i, 1);
            }
        }
    },
});
Scene.Assets.define("delay", {
    load(path, spec) {
        return new Promise(resolve => delays.push({ path, left: spec.seconds, resolve }));
    },
    free() {},
});

function hud(font) {
    const stats = Scene.Assets.stats();
    const kinds = Object.entries(stats.byKind).map(([k, n]) => `${k} ${n}`).join(", ");
    font.print(20, 400, `stack: ${Scene.stack.map(s => s.constructor.name).join(" > ")}`);
    font.print(20, 420, `assets: ${stats.entries} held (${kinds || "none"})`);
}

class Title extends Scene {
    static root = ROOT;
    static assets = { fonts: { big: { path: "font/Quicksand-Regular.ttf", size: 40, preload: true } } };
    enter(assets) {
        this.font = assets.fonts.big;
        this.time = 0;
        Scene.preload(Level);   // loads while the title runs; released when Level enters
    }
    update(dt) {
        this.time += dt;
        if (pad.justPressed(Gamepad.CROSS)) Scene.go(Level, { transition: "fade", duration: 0.6 });
    }
    draw() {
        this.font.print(180, 160, "Scene example");
        if (Math.floor(this.time * 2) % 2 === 0) this.font.print(200, 240, "press CROSS");
        hud(this.font);
    }
}

class Level extends Scene {
    static root = ROOT;
    static assets = {
        sheets: { slime: "sprite_sheet.json" },
        sfx: { pop: "sound/pop.adp" },
        music: { theme: { path: "sound/music.ogg", loop: true } },
        fonts: { big: { path: "font/Quicksand-Regular.ttf", size: 40, preload: true } },   // shared with Title
        delay: { slow: { path: "slow", seconds: 1 } },
    };
    enter(assets) {
        this.font = assets.fonts.big;
        this.slime = new Sprite.Instance(assets.sheets.slime, { clip: "bounce", origin: [0.5, 1], scale: 3 });
        this.slime.on("end:bounce", () => this.slime.play("idle"));
        this.x = 320;
        this.y = 300;
        assets.music.theme.play({ fade: 500 });
        // Runs when the level leaves, however it leaves.
        this.defer(() => assets.music.theme.stop());
    }
    update(dt) {
        const d = pad.dpad();
        this.x += (d.x || pad.leftX) * 150 * dt;
        this.y += (d.y || pad.leftY) * 150 * dt;
        if (pad.justPressed(Gamepad.CROSS)) this.assets.sfx.pop.play();
        if (pad.justPressed(Gamepad.START)) Scene.push(Pause);
        if (pad.justPressed(Gamepad.SELECT)) Scene.go(Title, { transition: "wipe", direction: "right" });
    }
    draw() {
        this.slime.draw(this.x, this.y);
        hud(this.font);
    }
    pause() { this.assets.music.theme.pause({ fade: 200 }); Loop.setTimeScale(0); }
    resume(result) {
        Loop.setTimeScale(1);
        if (result !== "quit") this.assets.music.theme.play({ fade: 200 });
    }
}

class Pause extends Scene {
    enter() { this.font = new Font(); }
    update() {
        if (pad.justPressed(Gamepad.START)) Scene.pop();
        if (pad.justPressed(Gamepad.CIRCLE)) {
            Scene.pop({ result: "quit" });
            Scene.go(Title, { transition: "fade" });   // queued after the pop
        }
    }
    draw() {
        Draw.rect(160, 140, 320, 120, Color.new(0, 0, 0, 90));
        this.font.print(250, 170, "Paused");
        this.font.color = GREY;
        this.font.print(190, 210, "START resume   CIRCLE quit");
        this.font.color = WHITE;
    }
    exit() { this.font.free(); }
}

Scene.onError = error => console.log("scene error: " + error.message);
Scene.run(Title, undefined, { clearColor: Color.new(24, 28, 40) });
// Gamepad and Sound need their per-frame calls: a system before the scenes.
Loop.addSystem({ name: "input", priority: -300, preUpdate() { Gamepad.update(); Sound.process(); } });
