<br />
<p align="center">
  <a href="https://github.com/GibranKhalil/AthenaEnv/">
    <img src="https://github.com/DanielSant0s/AthenaEnv/assets/47725160/f507ad9b-f9a1-4000-a454-ff824bc9d70b" alt="AthenaEnv logo" width="100%" height="auto">
  </a>
</p>

<p align="center">
  A modular JavaScript runtime for the PlayStation 2™
</p>

---

## Table of contents

- [About](#about)
- [Getting started](#getting-started)
- [Writing scripts](#writing-scripts)
- [Modules](#modules)
- [Configuration](#configuration)
- [Building from source](#building-from-source)
- [Native code](#native-code)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)
- [Credits](#credits)

## About

AthenaEnv runs modern JavaScript on the PlayStation 2. Scripts are executed by
a PS2-tuned build of [QuickJS](https://bellard.org/quickjs/) and reach the
hardware through native modules: GS rendering, VU1-accelerated tilemaps, 3D
and particles, IPU video decoding, SPU2 audio, controllers, storage, threads
and physics.

Features are **modules** that can be left out of the build. A game ships
only what it uses, so the binary and its RAM footprint stay small. The same
native modules can also be used from C, without the JavaScript engine;
modules written entirely in JavaScript require QuickJS.

Highlights:

- **Game loop with native timing**: `Loop.run()` provides the frame delta,
  fixed-step updates for physics, time scaling and frame statistics.
- **2D rendering**: primitives, textured images, TrueType and bitmap fonts,
  and a VU1-batched tilemap renderer for thousands of sprites.
- **Asynchronous assets**: images, sound effects and archives load on worker
  threads without stalling the frame.
- **Memory card saves**: JSON saves that survive a pulled card, awaitable
  without dropping frames, and `icon.sys` generation for the PS2 browser.
- **3D**: VU1 transform and diffuse lighting, native clipping, glTF/OBJ
  models with textures, a scene graph with hierarchical culling, keyframe
  animation, skinned meshes deformed on VU1, morph targets, level collision,
  walking characters and rigid bodies, camera controllers and tweens
  advanced in C, and billboard particles built on VU1.
- **Particles**: 2D and 3D emitters simulated in C and expanded into quads by
  VU1 programs; thousands per frame.
- **Box2D 3.2 physics** with a debug renderer.
- **MPEG-1/2 video** decoded by the IPU and usable as a texture.
- **Up to eight players**: PS2 pads, multitaps, DualShock 3/4 over USB and
  Bluetooth.
- **TypeScript declarations** for every module, generated for your exact build.

## Getting started

### 1. Get a build

Download `AthenaEnv.tar.gz` from the [Releases](https://github.com/GibranKhalil/AthenaEnv/releases)
page, or [build it yourself](#building-from-source). The archive contains the
`bin/` folder:

| File | Purpose |
|---|---|
| `athena.elf` | The runtime. |
| `athena_debug.elf` | Unstripped build with debug logging. |
| `athena.ini` | Boot configuration: which script to run, which drivers to start. |
| `main.js` | The default entry script. |
| `athena.d.ts`, `jsconfig.json` | Editor completion for every module of the build. |
| `tests/` | Module tests and examples. |

### 2. Write a script

Replace `main.js` with:

```js
const font = new Font();

Loop.run(() => {
    font.print(20, 20, "Hello, PS2!");
});
```

`Loop.run()` clears the screen, calls your function and presents the frame,
once per vertical blank.

### 3. Run it

- **PCSX2**: enable the host filesystem in the emulator settings, then boot
  `athena.elf` from the `bin/` folder. The emulator console shows
  `console.log()` messages and errors.
- **PS2**: copy the `bin/` folder to a USB drive or memory card and start
  `athena.elf` with an ELF launcher such as wLaunchELF.

Uncaught errors are shown on screen with their stack trace. Hardware
exceptions, such as an invalid memory access in native code, show an
exception screen with the CPU registers and the faulting function.

## Writing scripts

### Globals and imports

Modules with a JavaScript API expose globals (`Screen`, `Draw`,
`Gamepad`...). Driver-only modules have no JavaScript global. Modules can
also be imported explicitly, which documents the
dependencies of a file:

```js
import * as Screen from "Screen";
import { Font } from "Font";
```

`Font`, `Image`, `ImageList`, `Video` and `Scene` are classes; other globals
are namespaces that can also expose classes, such as `Scene3D.Node` and
`Input.Map`. A global is available only when its module is in the build.
QuickJS's `std` and `os` modules are
global as well, together with `setTimeout`, `setInterval`, `setImmediate` and
their `clear*` counterparts.

Your own files are regular ES modules:

```js
// utils.js
export const clamp = (value, min, max) => Math.min(Math.max(value, min), max);

// main.js
import { clamp } from "utils.js";
```

The QuickJS version in use (2021-03-27) has no top-level `await`: use it
inside an `async` function.

### Editor support

`bin/athena.d.ts` declares the API of every module in the build, with
documentation for each function. `bin/jsconfig.json` enables it in Visual
Studio Code and other editors that understand TypeScript declarations; no
TypeScript is required.

### The game loop

`Loop.run()` is the recommended way to drive a game. It takes a function, or an
object with `update` and `draw`:

```js
const pad = Gamepad.player(0);
const WHITE = Color.new(255, 255, 255);
let x = 320;

Loop.run(dt => {                       // dt: seconds since the previous frame
    Gamepad.update();
    x += pad.leftX * 200 * dt;         // 200 pixels per second at any frame rate
    Draw.rect(x, 200, 32, 32, WHITE);
});
```

```js
Loop.run({
    update(step) { world.step(step, 4); },   // 0..N times per frame, with a constant step
    draw(alpha) { Box2DDraw.draw(world); },  // once per frame
}, { fixedStep: 1 / 60 });
```

Options: `clear`, `clearColor`, `maxDelta`, `fixedStep`, `maxSteps` and
`vsyncInterval` (`2` holds a steady 30 FPS on NTSC). `Loop.setTimeScale()`
slows down or pauses game time, and `Loop.getStats()` reports the FPS and the
CPU time spent per frame.

Modules and games register per-frame work once as **systems**, which run
around the handlers every frame, by priority, until they are removed:

```js
Loop.addSystem({
    name: "hud",
    priority: 100,
    realTime: true,                        // keeps running with setTimeScale(0)
    postUpdate(dt) { /* animate */ },
    postDraw() { font.print(20, 20, `FPS ${Loop.getStats().fps | 0}`); },
});
```

The phases are `preUpdate(dt)`, `update(step)` (with each handler update),
`postUpdate(dt)`, `preDraw(alpha)` and `postDraw(alpha)`; C modules register
systems through `<athena/loop.h>`.

Unlike a `while (true)` loop, `Loop.run()` returns immediately and frames start
once the script ends, so timers, promises and `async` functions keep running
between frames. A manual loop is still possible:

```js
while (true) {
    Screen.clear();
    // draw...
    Screen.flip();
}
```

### Switching scripts

`std.reload(path)` ends the running script and starts another one in a fresh
JavaScript VM, without restarting the ELF: a title screen can start the game,
a menu can start a level pack. Nothing after the call runs; a path that
cannot be opened throws instead.

```js
std.reload("game/main.js");                                // switch for good
std.reload("tests/box2d_test.js", { returnTo: "menu.js" }); // come back afterwards
```

With `returnTo`, the new script returns to that one when it ends, throws, or
the player holds SELECT+START for a second, and `std.lastRun()` tells how it
went: `{ script, status, error, output }` with `status` `"finished"`,
`"error"`, `"exited"` (SELECT+START) or `"reloaded"`, and `output` the last
16 KiB it printed with `console.log()` or `print()`. State kept by native code, such as
loaded IOP drivers or the enabled gamepad drivers, is not reset between
scripts.

### Paths and devices

Paths are relative to the folder AthenaEnv was started from, or absolute with a
device prefix: `mass:/` (USB), `mc0:/` and `mc1:/` (memory cards), `cdrom0:`
(disc) and `host:` (the PC folder, **PCSX2 only**). `System.listDir()` and
`System.devices()` explore them.

### Numbers and memory

- `15.0f` is a single-precision float literal, and `Math.fround()` returns
  real single floats. The EE's FPU only computes in 32 bits.
- The JavaScript heap is limited to half of the RAM free at start-up.
  `System.getMemoryStats()` reports the binary size, the native heap, the
  QuickJS heap and its limit; `bin/tests/memory_stats.js` shows them on
  screen.
- Pixels, textures and glyphs live outside the JavaScript heap, so the
  collector cannot tell how much memory an unused `Image` or `Font` holds.
  Call `free()` on the ones you are done with (at a scene change, for
  example), and `std.gc()` on loading screens. Loading an image or font that
  runs out of memory collects garbage and tries once more.
- Deep recursion throws a catchable `InternalError: stack overflow` before it
  can overrun the stack: the main thread has 512 KB (`MAIN_STACK_SIZE` in the
  Makefile; about 1200 levels of JavaScript recursion), and `Thread.new()`
  threads 32 KB by default.

### Background jobs

Slow work runs on a small pool of worker threads while frames keep coming:
reading and writing memory card files, extracting archives, loading sound
effects and fonts. Every `*Async` function returns a job, with the same API
in every module:

```js
async function load() {
    const font = await Font.loadAsync("fonts/title.ttf", { size: 40 });   // await it...
    const save = await MemoryCard.readJSONAsync("mc0:/MYGAME/save.json");
    return { font, save };
}

const job = Archive.extractAsync("dlc.zip", "mass:/GAME/dlc");
Loop.run(() => {
    const status = job.poll();          // ...or poll it: { state, result | error, ...progress }
    if (status.state === "running") drawBar(status.bytesDone / status.bytesTotal);
});
```

`job.wait(timeoutMs)` blocks until it settles and `job.cancel()` stops it;
`MemoryCard.poll(job)`, `Archive.wait(job)` and the like do the same. Jobs
that wait for the IOP (memory card, disc, USB) run above the script thread,
the ones that use the CPU (decompression) below it. C modules submit their
own kinds of jobs through `<athena/job.h>` and expose them with
`<athena/js/job.h>`.

## Modules

The default build includes 2D graphics (`graphics`, `screen`, `loop`, `draw`,
`color`, `image`, `imagelist`, `font`, `tilemap`, `camera2d`, `sprite`),
`collision`, `gamepad`, `sound`, `video`, math (`vector`, `matrix4`, `random`,
`noise`), system and storage (`system`, `archive`, `iop`, `memcard`, `usbmass`,
`cdrom`, `poweroff`), and concurrency (`thread`, `mutex`, `timer`). All other
modules are optional; `system` is required in every build. The `default` and
`required` fields in each `src/modules/<id>/module.json` define this selection;
`node tools/modules.js list` shows the available modules.
Each module's API is documented in its TypeScript declaration, linked in the
tables below.

Browse [Graphics](#graphics), [3D](#3d), [Input](#input), [Audio](#audio),
[Animation](#animation), [Game structure](#game-structure),
[Physics](#physics), [Math](#math), [System and storage](#system-and-storage),
[Concurrency and timing](#concurrency-and-timing) and
[Native modules](#native-modules).

Examples using optional APIs require those modules in the selected build.
Custom builds must also include every API used by the script and a driver
for its boot device; see [Choosing modules](#choosing-modules).

### Graphics

| Module | Global | Description |
|---|---|---|
| [`screen`](src/modules/screen/screen.d.ts) | `Screen` | Video modes, clear and flip, VSync, FPS, VRAM statistics and GS state (alpha, depth, scissor). |
| [`loop`](src/modules/loop/loop.d.ts) | `Loop` | Runtime-driven game loop: frame delta, fixed steps, time scale, frame pacing and statistics. |
| [`draw`](src/modules/draw/draw.d.ts) | `Draw` | Points, lines, rectangles, circles, triangles and quads, flat or Gouraud shaded. |
| [`color`](src/modules/color/color.d.ts) | `Color` | Packing and editing of RGBA colors. |
| [`image`](src/modules/image/image.d.ts) | `Image` | PNG, JPEG and BMP loading, pixel access and textured drawing with tint, source rectangle and rotation; `drawList()` draws many sprites of one texture at once. |
| [`imagelist`](src/modules/imagelist/imagelist.d.ts) | `ImageList` | Asynchronous image loading with priorities, deduplication, an LRU cache and an optional decoder thread. |
| [`font`](src/modules/font/font.d.ts) | `Font` | TrueType and bitmap fonts at any size, loaded on a worker with `Font.loadAsync()`, glyphs preloaded a slice per frame with `preload()`, and cached layouts with `render()`; multi-line text, kerning, alignment, outline and drop shadow, with square glyphs on NTSC, PAL, 480p and 16:9. |
| [`tilemap`](src/modules/tilemap/tilemap.d.ts) | `TileMap` | VU1-accelerated batched sprites and tilemaps. |
| [`camera2d`](src/modules/camera2d/camera2d.d.ts) | `Camera2D` | 2D cameras applied in C to `Draw`, `Image`, `Font` and `TileMap`: position, zoom, rotation and viewport (split screen); follow with smoothing, dead zone, lookahead and auto zoom; bounds, rooms, shake, parallax, culling, timed zoom and pan, fades, flashes, letterbox and transitions between cameras. |
| [`sprite`](src/modules/sprite/sprite.d.ts) | `Sprite` | Spritesheets (grids, Aseprite and TexturePacker JSON with trimmed frames and tags) and animation clips (fps or per-frame durations, loop, once and pingpong, frame events), animated sprites with origin, scale, rotation and flip, and batch animation of `TileMap` sprites, all advanced in C by the `Loop`. |
| [`video`](src/modules/video/video.d.ts) | `Video` | MPEG-1/2 playback on the IPU, drawn directly or used as an `Image`. |
| [`particles2d`](src/modules/particles2d/particles2d.d.ts) | `Particles2D` | Particle emitters simulated in C (pool, rate and bursts, gravity, drag, size and color over life, rotation and spin) and drawn by a VU1 program that builds each rotated quad through the 2D camera. Not in the default build. |
| `graphics` | — | GS initialization and the rendering core shared by the modules above. |

```js
const logo = new Image("logo.png");
const font = new Font("fonts/title.ttf", { size: 40 });   // rasterized at 40 px

Loop.run(() => {
    logo.draw(100, 80);
    font.print(100, 300, "Press START\nto continue");
}, { clearColor: Color.new(20, 20, 40) });
```

Games draw in world coordinates and let a camera place them. `Camera2D.main`
is applied around `Loop.run()`'s `draw` from the first time it is used, and
shows the screen unchanged until it moves:

```js
const cam = Camera2D.main;
cam.follow(player, { lerp: 8, deadzone: { w: 64, h: 32 }, lookahead: 40 });
cam.setBounds(0, 0, level.width, level.height);

Loop.run({
    update(dt) {
        player.update(dt);
        if (player.hit) cam.shake(6, 0.3);
    },
    draw() {
        cam.draw(() => sky.draw(0, 0), { parallax: 0.3 });   // a slower layer
        map.render(0, 0);                                     // TileMap, world space
        hero.draw(player.x, player.y);                        // Image, world space
        Camera2D.screenSpace(() => font.print(10, 10, `HP ${player.hp}`));
    },
});
```

For split screen, give each camera a `viewport`, call
`Camera2D.setCurrent(null)` and draw the world once per camera with
`cam.draw(fn)`. Smoothing is frame-rate independent; `zoomTo()`, `panTo()`,
`fade()`, `flash()`, `letterbox()` and `Camera2D.transition()` return
promises. Under a rotation, rectangles become two triangles (TileMaps stay on
VU1, as triangle strips). `TileMap.setCamera()` still works and combines with
the camera.
`bin/tests/camera2d_example.js` shows it all, split screen included.

More: `follow(p, { interpolate: true })` for `fixedStep` games that draw
positions blended by `alpha`, `zoomBySpeed`, `addTrauma()` and `kick()` for
impacts, `realTime` cameras that keep fading while the game is paused,
`Camera2D.push()`/`pop()` for cutscenes, `drawRepeat()` for repeating skies,
`state()`/`setState()` for saves, `cam.debug = true` to see the dead zone,
bounds and zones, and `Camera2D.getStats().culled`.
`bin/tests/camera2d_bench.js` measures the cost per frame on the console.

Animated sprites play clips of a spritesheet, advanced in C by the `Loop`
(so `Loop.setTimeScale()` pauses them) and drawn through the camera:

```js
const sheet = Sprite.Sheet.fromGrid(new Image("hero.png"), {
    frameWidth: 32, frameHeight: 32,
    clips: {
        idle: { frames: "0-3", fps: 6 }, run: "4-11",
        hit: { frames: "12-14", mode: "once", next: "idle" },   // back to idle when it ends
    },
});
// or Sprite.Sheet.fromJSON("hero.json"): Aseprite and TexturePacker, tags become clips
const hero = new Sprite.Instance(sheet, { clip: "idle", origin: [0.5, 1] });
hero.on("run:3", () => footstep.play());   // position 3 of the run clip

Loop.run({
    update() { hero.play(moving ? "run" : "idle"); hero.flipX = facingLeft; },
    draw() { hero.draw(player.x, player.y); },
});

// Many instances: one call, one GS packet per 128 sprites.
Sprite.drawAll(enemies, positions);           // positions: Float32Array [x0, y0, ...]

// Hundreds of coins on one TileMap, animated in C: one render() for all.
Sprite.Animator.bind(coins, coinSheet, "spin", { randomStart: true });
```

`await Sprite.Sheet.fromJSONAsync("hero.json")` loads a sheet and decodes its
texture on a worker (and `fromGridAsync` for grids), so loading screens keep
drawing. `await hero.playAsync("die")` waits for a clip; `realTime: true` keeps menus
animating while the game is paused; Aseprite slices become hitboxes
(`hero.getSlice("hit")`), and `hero.debug = true` (or `Sprite.setDebug(true)`)
draws outlines, origins and slices; TexturePacker frames rotated in the atlas
are drawn turned back; clips can take a grid row (`{ row: 2, fps: 8 }`);
`inset: 0.5` on a sheet stops filtering from showing neighbor frames. `bin/tests/sprite_example.js` shows the API;
`bin/tests/sprite_bench.js` compares it with animation written in JavaScript.

Particle effects need no per-particle JavaScript: an emitter is configured
once, moved, and drawn with one call. 2000 particles cost about 1.7 ms per
frame on the PS2; the same effect written in JavaScript costs 21 ms for 300.

```js
const sparks = new Particles2D.Emitter(new Image("spark.png"), {
    capacity: 800, rate: 400, life: [.6, 1.2], speed: [80, 160],
    angle: -Math.PI / 2, spread: 1, gravity: [0, 200], size: [10, 2],
    color: [Color.new(255, 220, 120, 128), Color.new(255, 40, 0, 0)], spin: [-4, 4],
});
Particles2D.attachLoop();
Loop.run({
    update() { sparks.setPosition(player.x, player.y); },
    draw() { sparks.draw(); },
});
```

`image.drawList()` takes the sprite records of `TileMap.SpriteBuffer` (x, y,
w, h, u1, v1, u2, v2, r, g, b, a): the texture state goes out once per 128
sprites instead of once per `draw()`, for particles, bullets and tiles that
change every frame. Fonts share glyph caches between equal loads (same file
and size); at most 16 different ones are loaded at a time, so `free()` the
ones no longer used.

### 3D

Not in the default build: `node tools/modules.js configure --modules=scene3d,screen,usbmass`
(dependencies such as `render3d`, `model3d` and `camera3d` come with it). The
screen needs a depth buffer: `mode.zbuffering = true` before `Screen.setMode()`.

| Module | Global | Description |
|---|---|---|
| [`quaternion`](src/modules/quaternion/quaternion.d.ts) | `Quaternion` | Rotations: axis-angle, Euler (`Rz·Ry·Rx`), multiply, slerp and TRS matrices. |
| [`camera3d`](src/modules/camera3d/camera3d.d.ts) | `Camera3D` | Perspective cameras with reversed depth, look-at and frustum tests. |
| [`model3d`](src/modules/model3d/model3d.d.ts) | `Model3D` | One mesh from glTF/GLB or OBJ, or geometry from typed arrays; materials (unlit, diffuse) and textures; instances with their own transform; bulk position/rotation setters. Whole scenes with animations: `GLTF3D`. |
| [`lights`](src/modules/lights/lights.d.ts) | `Lights` | Ambient, up to four directional and four point lights (lit per vertex on VU1, with distance falloff), and distance fog applied by the GS. |
| [`render3d`](src/modules/render3d/render3d.d.ts) | `Render3D` | Opaque rendering on VU1: color, Gouraud diffuse and perspective-correct textures, culling, objects crossing the screen edges drawn on VU1 inside the GS guard band, near-plane clipping on VU1 for objects crossing the camera, clipping in C for the rest, and batches that share one GS/VU1 pass per pipeline. |
| [`gltf3d`](src/modules/gltf3d/gltf3d.d.ts) | `GLTF3D` | glTF/GLB scenes: node hierarchy as Scene3D nodes, every primitive as a mesh, animations as Animation3D clips, skinned meshes deformed by their joints (on VU1 with a bone palette of up to 24 joints when the mesh is fully on screen, in C with clipping otherwise), and morph targets blended by animated or scripted weights (on VU1 for up to four active targets). |
| [`scene3d`](src/modules/scene3d/scene3d.d.ts) | `Scene3D` | Scene graph: hierarchy with dirty propagation, subtree bounds culling, a render queue sorted by pipeline, native motion (velocity and spin), skins and morph weights, and an update system on the Loop. |
| [`particles3d`](src/modules/particles3d/particles3d.d.ts) | `Particles3D` | Billboard particles: emitters in C, quads built and projected by VU1, depth tested against the scene without writing depth. |
| [`meshbuilder`](src/modules/meshbuilder/meshbuilder.d.ts) | `MeshBuilder` | Native procedural geometry: boxes, spheres, cylinders, planes and heightmaps, transformed mesh merging, vertex colors and atlas UVs; splits large builds into multiple Model3D meshes. |
| [`voxel`](src/modules/voxel/voxel.d.ts) | `Voxel` | Editable block worlds in native memory, terrain and caves, chunk meshing within a time budget, distance/frustum culling, block raycasts and box movement. |
| [`lod`](src/modules/lod/lod.d.ts) | `LOD` | Distance-based mesh selection for Scene3D nodes, hysteresis, quality bias, draw-distance culling and optional fog settings. |
| [`sky`](src/modules/sky/sky.d.ts) | `Sky` | Sky gradient, sun disc and day/night keyframes; applies matching ambient, directional light and fog color to a Lights.Set. |
| [`debug3d`](src/modules/debug3d/debug3d.d.ts) | `Debug3D` | World-space lines, boxes, spheres, axes, grids, frusta and mesh normals, projected and clipped in C and drawn over the scene. |

Animation and cameras for 3D objects are listed under [Animation](#animation):
`Animation3D`, `CameraRig3D` and `Tween3D`.

```js
const mode = Screen.getMode();
mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({ aspect: mode.width / mode.height, near: 1, far: 100 });
const lights = new Lights.Set().setAmbient(.3, .3, .3).setDirectional(0, .4, .6, 1, .8, .8, .8);
const crate = Model3D.load("models/crate.glb", { shading: Model3D.DIFFUSE });

const scene = new Scene3D.Scene(), root = scene.root;
const box = new Scene3D.Node(crate).setPosition(0, 0, -5).setSpin(0, 1.5, 0); // turns in C
root.add(box); root.dispose();

scene.attachLoop();                                   // advance + update every frame
CameraRig3D.attachLoop();
new CameraRig3D.Orbit(camera, box).setAngles(0, .3).setSharpness(6, 6);
Tween3D.attachLoop();
Tween3D.to(box, { scale: [1.5, 1.5, 1.5] }, .8, { ease: "outBack", yoyo: true, repeat: Infinity });

const stats = {};                                     // reused: no object per frame
Loop.run({ draw() { scene.draw(camera, Render3D.CULL_BACK, lights, stats); } });
```

Players, camera rigs and tweens act by themselves in native systems, so they
stay active until `dispose()` (or, for tweens, until they end), even when no
variable holds them; a script that ends releases what it created.

#### Procedural worlds and visibility

`MeshBuilder.Builder` keeps a current color, transform and UV rectangle for
the geometry added next. `build(material)` returns an array of meshes;
dispose the builder after building and keep the meshes for drawing or
Scene3D nodes. `merge()` bakes static props into shared geometry to reduce
draw calls; `heightmap()` accepts a `Float32Array`, including Noise output.

```js
const builder = new MeshBuilder.Builder();
const meshes = builder.color(.4, .6, .3)
    .plane(0, 0, 0, 40, 40, 8, 8)
    .build({ shading: Model3D.DIFFUSE });
builder.dispose();
for (const mesh of meshes) scene.root.add(new Scene3D.Node(mesh));
```

`Voxel.World` stores one byte per block (0 is air, 1–255 are material ids).
Edits mark chunks and affected neighbors dirty. Call `rebuild(budgetMs,
x, y, z)` each frame to rebuild near the player first, then `draw()`;
`rebuild(0)` builds everything during a loading screen. The budget is checked
between chunks, so at least one chunk is rebuilt even if it exceeds the
budget. Naive meshing supports atlas textures and ambient occlusion; greedy
meshing merges faces and supports colors without atlas textures or AO.
`raycast()` selects blocks and `moveBox()` handles block collisions.
Start with small worlds and inspect `stats().meshBytes`: mesh memory is in
addition to block storage. [bin/voxel_example.js](bin/voxel_example.js) shows
editing and first-person movement.

`LOD.Group` retains a node and up to eight meshes with increasing `until`
distances. `LOD.setCamera(camera)` selects them every frame; beyond the last
distance the node is hidden. Hysteresis prevents repeated swaps near a
threshold. Groups control their node's `visible` flag until disabled.
Selection uses the last scene update's world position and runs before the
next one; with a manual loop, call `LOD.update(camera)` before
`scene.update()`. `setBias()` scales thresholds and `setDrawDistance()` can
configure a Lights.Set's fog to soften the visibility cutoff.

Draw `Sky.draw(camera)` before the scene. `Sky.setTime(hours)` wraps a
24-hour clock and `Sky.apply(lights)` updates ambient light, directional
slot 0 and the color of enabled fog. Use DIFFUSE materials for objects that
should react to daylight; UNLIT materials and baked voxel lighting keep
their colors. The sky gradient ignores camera roll.

`Debug3D.setCamera(camera)` draws queued shapes after the Loop's draw, with
no depth test, so diagnostics stay visible. Shapes last one frame by
default, or a supplied number of real-time seconds. `lines(Float32Array)`
queues many segments at once; `dropped()` reports queue overflow.
See [bin/debug3d_example.js](bin/debug3d_example.js) and
[bin/world_systems_example.js](bin/world_systems_example.js), which combines
procedural geometry, LOD, navigation, triggers and a day/night cycle.

Systems run in this order within a frame: `Tween3D` (pre-update), then
`Animation3D`, `Scene3D` (advance and update) and `CameraRig3D` (post-update),
so cameras read world transforms of the same frame. `Render3D.Batch` draws
`Model3D.Instance` lists without a scene graph. Performance notes and
profiling examples are in [bin/3d_profile.js](bin/3d_profile.js) and
[bin/3d_regression.js](bin/3d_regression.js), with native counterparts in
`samples/native/3d_profile/` and `samples/native/3d_regression/`.

### JavaScript or native?

QuickJS on the EE is an interpreter: a call from JavaScript into C costs
about 7–9 µs, `Math.sin()` about 23 µs, and creating an object with ten
properties about 110 µs, while the C side transforms a vertex batch or a
quaternion in a few microseconds. Measured on PCSX2:

| Work per frame | In JavaScript | In C |
|---|---|---|
| 64 bobbing cubes with tweens | 8.0 ms (`Tween` + one setter each) | 0.11 ms (`Tween3D`) |
| 64 spinning nodes | 0.97 ms (`setRotationEuler` each) | 0.44 ms (`setSpin` once) |
| Follow camera | 43 µs | 12 µs (`rig.update()`), 0 with `attachLoop()` |
| 300 particles | 21 ms | 0.3 ms (`Particles2D`) |

So: describe in JavaScript (create, configure, react to events, run menus and
scenes) and let C do what repeats for every object every frame. Prefer native
systems (`attachLoop()`), batches (`Render3D.Batch`, `Scene3D`) and reusable
result objects (`draw(..., stats)`, `update(stats)`). Bulk setters
(`Scene3D.setPositions()`) help when the values are already in a
`Float32Array` (physics output, precomputed animation); filling one per frame
in JavaScript costs as much as the calls it saves.

Inside C, VU1 is the next step: GLTF3D uses a VU1 bone palette for eligible
skinned meshes, with an EE fallback when clipping is required. Inspect the
render statistics in your own scenes to identify expensive paths.

### Input

| Module | Global | Description |
|---|---|---|
| [`gamepad`](src/modules/gamepad/gamepad.d.ts) | `Gamepad` | Up to eight players: DualShock 2 on both ports and multitaps, DualShock 3/4 over USB and Bluetooth. Buttons, sticks, pressure and rumble. |
| [`input`](src/modules/input/input.d.ts) | `Input` | Named actions, button combinations and edges, digital axes, sticks with radial dead zones, response curves, sensitivity and d-pad fallback; rebinding and serializable bindings. Written in JavaScript; optional. |

```js
Gamepad.configure({ multitap: true });   // optional drivers: multitap, usb, bluetooth
const pad = Gamepad.player(0);

Loop.run(() => {
    Gamepad.update();                    // once per frame
    if (pad.justPressed(Gamepad.CROSS)) pad.rumble(0.5, 0, 200);
});
```

For gameplay actions, enable `input` and update the map after the pad:

```js
const controls = new Input.Map({
    move: Input.stick("left", { dpad: true, deadZone: .15 }),
    jump: Input.button(Gamepad.CROSS),
});
Loop.run({
    update(dt) {
        Gamepad.update();
        controls.update();
        player.x += controls.x("move") * 120 * dt;
        if (controls.justPressed("jump")) player.jump();
    },
    draw() { player.draw(); },
});
```

`Input.button(L1 | R1)` requires both buttons; separate arguments are
alternatives. `rebind()` changes an action, and `bindings()`/`load()` save
and restore bindings as plain data. Maps read raw sticks and apply their
own radial dead zone, setting their Gamepad player's `deadzone` to zero.
`setSource()` can read a Replay snapshot instead of a live controller.

### Audio

| Module | Global | Description |
|---|---|---|
| [`sound`](src/modules/sound/sound.d.ts) | `Sound` | ADPCM sound effects on the 24 SPU2 voices, loaded sync or on a worker with `Sound.loadSfxAsync()`, and one streamed WAV or Ogg Vorbis music track, with fades. |
| [`audio3d`](src/modules/audio3d/audio3d.d.ts) | `Audio3D` | Positional Sound.Sfx sources at world positions or following Scene3D nodes; camera listener, distance attenuation and stereo pan applied to playing SPU2 voices in C. Optional. |

```js
const music = new Sound.Stream("music/theme.ogg");
music.loop = true;
music.play({ fade: 1000 });

const jump = new Sound.Sfx("sfx/jump.adp");   // convert WAV files with tools/wav2adp.js
const pad = Gamepad.player(0);

Loop.run(() => {
    Gamepad.update();
    if (pad.justPressed(Gamepad.CROSS)) jump.play();
    Sound.process();                          // runs the onEnd and onLoop callbacks
});
```

For spatial effects, enable `audio3d`, set the listener and keep a source
for each sound that moves:

```js
Audio3D.setListener(camera);
const engine = new Audio3D.Source(engineSfx, {
    node: vehicleNode, minDistance: 2, maxDistance: 40, rolloff: "inverse",
});
engine.play();
```

The module updates voice levels after the scene and camera systems in
`Loop.run()`; manual loops call `Audio3D.update()` after updating transforms.
`play()` returns -1 when out of range or no voice is free. A source retains
its Sfx; `dispose()` stops and releases the source. SPU2 output is stereo:
position affects volume and pan, without front/back filtering or occlusion.

### Animation

Not in the default build: `node tools/modules.js configure --modules=tween,draw,usbmass`

| Module | Global | Description |
|---|---|---|
| [`ease`](src/modules/ease/ease.d.ts) | `Ease` | Easing curves (Penner families, steps, cubic-bezier) and interpolation helpers: lerp, remap, smoothstep and frame-rate independent damping. Written in JavaScript. |
| [`tween`](src/modules/tween/tween.d.ts) | `Tween` | Tweens: animate numeric and color properties of any object with easing, delays, repeats and yoyo; awaitable, driven by the Loop. Written in JavaScript. |

```js
const logo = { x: 0, y: -50 };
const camera = { x: 0 };
const WHITE = Color.new(255, 255, 255);

async function intro() {
    // Tweens are awaitable: this resolves when the drop completes.
    await Tween.to(logo, { y: 150 }, 0.8, { ease: "outBounce" });
    // Then sway between x = 0 and 200 forever.
    Tween.to(logo, { x: 200 }, 1.0, { ease: "inOutQuad", yoyo: true, repeat: Infinity });
}
intro();

Loop.run(dt => {                                      // tweens advance by themselves
    camera.x = Ease.damp(camera.x, logo.x, 8, dt);   // same follow speed at 30 and 60 FPS
    Draw.rect(320 + logo.x - camera.x, logo.y, 32, 32, WHITE);
});
```

`Tween.to()` also interpolates packed colors per channel (`colors: ["tint"]`),
and `realTime: true` keeps menu animations running while
`Loop.setTimeScale(0)` pauses the game.

For 3D objects, the native modules below animate in C with no JavaScript per
frame (see [3D](#3d)):

| Module | Global | Description |
|---|---|---|
| [`tween3d`](src/modules/tween3d/tween3d.d.ts) | `Tween3D` | `Tween` for `Scene3D.Node`, `Model3D.Instance` and `Camera3D.Camera`: position, scale, rotation (slerp) and camera target, with the `Ease` curves in C, delay, repeat, yoyo, overwrite and awaitable handles. |
| [`animation3d`](src/modules/animation3d/animation3d.d.ts) | `Animation3D` | Keyframe clips (position, rotation, scale, morph weights; linear or step) sampled in C and played on Scene3D nodes. |
| [`camerarig3d`](src/modules/camerarig3d/camerarig3d.d.ts) | `CameraRig3D` | Follow and orbit controllers for `Camera3D` with limits, input deltas, auto-rotation and frame-rate independent smoothing. |

### Game structure

Not in the default build: `node tools/modules.js configure --modules=scene,usbmass`

| Module | Global | Description |
|---|---|---|
| [`scene`](src/modules/scene/scene.d.ts) | `Scene` | Scenes with a lifecycle, a stack for pause menus, fade transitions and a loading screen; reference-counted assets (images, sprite sheets, sounds, music, fonts, JSON) loaded in the background and kept while two scenes share them. Written in JavaScript. |
| [`assets3d`](src/modules/assets3d/assets3d.d.ts) | `Assets3D` | Adds meshes, textures3d and gltf kinds to Scene.Assets manifests, shared and disposed by reference count. Loads queued between frames. Written in JavaScript; optional. |
| [`replay`](src/modules/replay/replay.d.ts) | `Replay` | Compact controller recording and playback for up to eight players, with a stored seed, quantized sticks and snapshots compatible with Input.Map. Written in JavaScript; optional. |
| [`nav`](src/modules/nav/nav.d.ts) | `Nav` | Native A* on a weighted XZ grid, diagonal movement without corner cutting, path smoothing, and crowds with arrival and separation that drive Scene3D nodes. Optional. |
| [`triggers3d`](src/modules/triggers3d/triggers3d.d.ts) | `Triggers3D` | Box and sphere zones with layer masks and enter/exit events for spherical bodies; zones and bodies can follow Scene3D nodes. Pair tests in C, callbacks in JavaScript. Optional. |

```js
class Level1 extends Scene {
    static root = "assets/level1";
    static assets = {
        images: { tiles: { path: "tiles.png", upload: "lock" } },
        sheets: { hero: "hero.json" },
        sfx:    { jump: "jump.adp" },
        fonts:  { hud: { path: "hud.ttf", size: 20, preload: true } },
    };
    enter(assets) { this.hero = new Sprite.Instance(assets.sheets.hero, { clip: "idle" }); }
    update(dt) { if (pad.justPressed(Gamepad.START)) Scene.push(PauseMenu); }
    draw() { this.hero.draw(this.x, this.y); }
}

Scene.run(Title);                                          // starts the Loop
// from a scene:
Scene.go(Level1, { transition: "fade", duration: 0.5 });
```

`Scene.go()` loads the next scene's assets while the current one fades out,
and releases only what no other scene holds; a loading screen shows when
loading outlasts the fade. `Scene.push()`/`pop({ result })` stack scenes
(`pause()`/`resume(result)`) and `Scene.replace()` swaps only the top one.
Transitions are `"fade"`, `"wipe"` (with a `direction`), or your own
(`Scene.defineTransition()`, `{ draw(amount, info) }`). Data files (`data`,
`text`, `binary`) are read on the job pool with `Thread.readFileAsync()`.
`Scene.preload(Level)` loads the next scene while the current one runs;
`Scene.loadTimeout`/`timeout` fail a scene that does not load in time, and
`Scene.slowLoadWarning` logs which asset is still loading;
`this.defer(fn)` and `this.acquire(manifest)` tie clean-ups and extra assets
to a scene's lifetime. `Scene.Assets.stats()` shows what is held (for finding
VRAM leaks), and `Scene.Assets.define()` adds asset kinds.
`bin/tests/scene_example.js` shows it all.

`Assets3D` registers its kinds when imported. Add `meshes`, `textures3d`
and `gltf` entries to a scene's asset manifest; include `gltf3d` explicitly
when using the `gltf` kind, since it is not an Assets3D dependency.
`Assets3D.setBudget(ms)` controls the queued loading budget (default 8 ms
per frame). Model parsing is synchronous and at least one load runs per
frame: a single large model can exceed the budget. These loads are spread
across frames rather than decoded on a worker. Shared assets are disposed
when their last scene releases them.

For repeatable input, call `Replay.Recorder.capture()` once per simulation
step and let gameplay read `recorder.source(0)` through `Input.Map`.
Save with `recorder.save(path)`; load with `Replay.load(path)`, then call
`playback.advance()` before updating controls each step. Use a fixed step
and seed the same Random generators from the recording's `seed` in both
runs. Recorded sticks are quantized to 1/127, so gameplay must read the
recorded snapshots during capture too. The file stores input and a seed;
reproducing a run also requires the same initial game state and game logic.
Replay has no required module dependencies; include `gamepad`, `input`,
`random` and `loop` when using them in this workflow.

`Nav.Grid` stores costs from 0 (blocked) to 255; configure it from your
level data. `findPath()` returns world XZ pairs or null, and `Nav.Crowd`
advances agents with `crowd.update(dt)`. Navigation keeps each agent's Y
position; populate the grid and handle terrain height/collision in the game.
It has no automatic Loop attachment. Move bound agents in the game update,
before Scene3D's post-update system computes their world transforms.

`Triggers3D.World` also requires an explicit `update()`. If its bodies or
zones follow nodes, run it after `scene.update()` so callbacks see the
current world positions. With `scene.attachLoop()` at its default priority,
register a later post-update system:

```js
const triggers = new Triggers3D.World();
triggers.body(0, 0, 0, { radius: .4, layers: 1 }).follow(heroNode);
triggers.box(10, 0, -2, 12, 3, 2, {
    mask: 1, onEnter: () => console.log("Checkpoint reached"),
});
const triggerSystem = Loop.addSystem({
    name: "triggers", priority: 100,
    postUpdate() { triggers.update(); },
});
// On leaving the level: Loop.removeSystem(triggerSystem); triggers.dispose();
```

The masks filter event recipients; these zones report overlaps and do not
resolve physical collisions. Dispose grids, crowds, trigger worlds and LOD
groups when a level releases them.

### Physics

| Module | Global | Description |
|---|---|---|
| [`collision`](src/modules/collision/collision.d.ts) | `Collision` | Light collision and simple physics in C: rectangles and circles in a spatial hash, tile grids with solid tiles, one-way platforms and slopes, swept movement that slides on walls and never tunnels, gravity, bounce, moving platforms that carry riders, layers and masks, queries, pairs and raycasts. |
| [`collision3d`](src/modules/collision3d/collision3d.d.ts) | `Collision3D` | Light 3D collision in C: static triangles of level meshes, glTF scenes and boxes in a BVH; raycasts (also many per call), sphere casts and overlaps with layers; kinematic characters that walk with collide-and-slide (gravity, walkable slopes, steps, ground snapping, no tunneling) and drive a Scene3D node from one Loop system. Not in the default build. |
| [`physics3d`](src/modules/physics3d/physics3d.d.ts) | `Physics3D` | 3D rigid bodies in C: dynamic, kinematic and static spheres, boxes and capsules with friction, restitution, rolling resistance, warm starting and island sleeping, against each other and the static level of a `Collision3D` world; ball, hinge (limits, motor), distance/rope and weld joints; bodies drive Scene3D nodes, each world steps in the Loop. Not in the default build. |
| [`box2d`](src/modules/box2d/box2d.d.ts) | `Box2D` | Box2D 3.2: worlds, bodies, five shape types, chains, seven joint types, ray and shape casts, overlap queries, character movers, events and snapshots. Not in the default build. |
| [`box2ddraw`](src/modules/box2ddraw/box2ddraw.d.ts) | `Box2DDraw` | Debug drawing of a Box2D world. Not in the default build. |

`Collision` is for platformers, top-down games and shooters that want
predictable, tile-friendly movement; Box2D is for rigid bodies, joints and
polygons. Worlds step themselves with the Loop, before the game's `update`.

Physics objects reach the screen without a loop over bodies in JavaScript:
`world.readTransforms()` writes x, y and angle per body into a
`Float32Array`, which `Sprite.drawAll()` (with `{ stride: 3 }`) and
`Scene3D.setTransforms2D()` take as it is:

```js
const transforms = new Float32Array(bodies.length * 3);
Loop.run({
    draw() {
        world.readTransforms(bodies, transforms);                 // x, y, angle
        Sprite.drawAll(crates, transforms, { stride: 3 });        // 2D sprites, rotated
        // or, for a 2.5D game: Scene3D.setTransforms2D(nodes, transforms);
    },
});
```

```js
const SOLID = 1, PLAYER = 2, COIN = 4;
const world = new Collision.World({ gravity: { x: 0, y: 900 } });
world.setGrid({
    columns: 40, rows: 15, tileWidth: 16, tileHeight: 16,
    tiles: levelIds,                                   // the ids given to TileMap
    solid: [1, 2, 3], oneWay: [4], slopes: { 5: "45r", 6: "45l" },
});
const player = world.add({ type: "dynamic", x: 32, y: 32, w: 12, h: 24,
    layer: PLAYER, mask: SOLID | COIN });
const lift = world.add({ type: "kinematic", x: 200, y: 160, w: 48, h: 8 });
Tween.to(lift, { x: 320 }, 2, { yoyo: true, repeat: Infinity });   // carries the player
world.add({ x: 300, y: 100, r: 6, sensor: true, layer: COIN });
world.onEnter = (sensor, body) => { if (sensor.layer === COIN) sensor.remove(); };
const pad = Gamepad.player(0);

Loop.run({
    update() {
        Gamepad.update();
        player.vx = pad.pressed(Gamepad.RIGHT) ? 120 : pad.pressed(Gamepad.LEFT) ? -120 : 0;
        if (pad.justPressed(Gamepad.CROSS) && player.onGround) player.vy = -330;
    },
    draw() { world.drawDebug(); },                    // outlines, through the camera
});
```

`world.move(body, dx, dy)` moves any body with collisions, like a character
controller, and tells what stopped it (pass a result object to reuse it and
allocate nothing); `world.raycast()`, `query()`, `overlapping()` and
`solidAt()` answer line-of-sight, area and "is there floor ahead?" questions
without a loop over every body in JavaScript. Sensors report
`world.onEnter(sensor, body)` and `onExit()` for pickups and triggers.
Kinematic bodies moved by setting `x`/`y` (a `Tween`, a path) carry their
riders and push what is in their way; a body caught against a wall is
`crushed`. `bin/tests/collision_example.js` is a playable
level, and `samples/native/collision` the same from C.

### Math

| Module | Global | Description |
|---|---|---|
| [`vector`](src/modules/vector/vector.d.ts) | `Vector` | `Vector2`, `Vector3` and `Vector4` with PS2 alignment. |
| [`matrix4`](src/modules/matrix4/matrix4.d.ts) | `Matrix4` | 4×4 transformation matrices. |
| [`random`](src/modules/random/random.d.ts) | `Random` | Seedable generators (xoshiro128**): integers, floats, booleans, gaussian, pick, shuffle, weighted choice and a state you can save. |
| [`noise`](src/modules/noise/noise.d.ts) | `Noise` | Perlin and simplex (2D/3D), Worley and fBm in C, with batch fills of `Float32Array` grids and classification into tile ids. |

```js
const rng = new Random.Generator("run-42");   // same seed, same run
const loot = rng.pick(["sword", "shield", "potion"]);
const rarity = rng.weighted([70, 25, 5]);      // index 0, 1 or 2

// A 64x64 island map in two native calls, no per-cell loop in JavaScript.
const height = new Noise.Generator("island-" + rng.int(0, 1e6));
const heights = height.fill(new Float32Array(64 * 64), 64, 64,
    { scale: 0.05, mode: "ridged", warp: 0.5 });
const tiles = Noise.toTiles(new Uint16Array(64 * 64), heights,
    [0.3, 0.5, 0.8], [WATER, SAND, GRASS, ROCK]);
map.setTiles(0, tiles);                        // a TileMap.Instance
```

`rng.state()` returns four numbers that survive `JSON.stringify()`, so a
save file can resume the sequence with `rng.setState()`. For particles,
`rng.fill(array, min, max)` and `rng.fillGaussian(array, mean, stddev)`
fill a typed array in one call. Large maps can be generated without
stalling the frame: `fillAsync()` returns a `Job` (await it, `poll()` its
`rowsDone`, or `cancel()` it) computed on the worker pool.
`bin/tests/noise_example.js` draws a generated island and switches modes live.

### System and storage

| Module | Global | Description |
|---|---|---|
| [`system`](src/modules/system/system.d.ts) | `System` | Files and folders, devices, hardware information, memory statistics, timing, garbage collection and launching ELFs. Always included. |
| [`archive`](src/modules/archive/archive.d.ts) | `Archive` | Reads zip, tar, tar.gz and gzip; safe extraction, also on a worker thread; gzip in memory. |
| [`iop`](src/modules/iop/iop.d.ts) | `IOP` | IOP driver discovery, loading, reset and memory statistics. |
| [`debug`](src/modules/debug/debug.d.ts) | `Debug` | On-screen diagnostics: stats overlay (FPS, CPU and frame time, RAM, JS heap, VRAM) with a frame-time graph, watches, the script's console output, shapes and text in screen or world space for a set time, and a controller shortcut to show and hide it all. Not in the default build. |
| [`profiler`](src/modules/profiler/profiler.d.ts) | `Profiler` | Native CPU timer scopes and per-frame counters, a 120-frame history with average, p95 and peak, Render3D totals and Debug overlay integration. Optional; includes Loop and Debug. |
| [`bench`](src/modules/bench/bench.d.ts) | `Bench` | Frame-paced benchmark tasks with warmup, native timing, repeated batches, average/p95/peak, cleanup, cancellation and JSON checkpoints. Written in JavaScript; optional, includes Profiler. |
| [`memcard`](src/modules/memcard/memcard.d.ts) | `MemoryCard` | Memory cards on `mc0:/` and `mc1:/`: card status and swap detection, files (whole, JSON or streamed), directories, attributes and dates, atomic saves, `icon.sys`, format, and every slow call also as an awaitable background job. Also the drivers the memory card boot device needs. |
| [`savegame`](src/modules/savegame/savegame.d.ts) | `SaveGame` | Versioned JSON save slots over MemoryCard, CRC-32 validation, atomic background writes, migrations, browser icons and optional gzip when Archive is included. JavaScript with a native codec; optional. |
| `usbmass` | — | USB storage drivers (`mass:/`). |
| `mx4sio` | — | MX4SIO (SD card adapter in memory card slot 2) drivers (`mass:/`). Not in the default build; slot 2 no longer reads memory cards while it is loaded. |
| `hdd` | — | Internal hard disk drivers for exFAT/FAT32 disks, not PFS (`mass:/`). Not in the default build. |
| `ilink` | — | i.LINK (IEEE 1394) storage drivers (`mass:/`), on the consoles that have the port. Not in the default build. |
| `cdrom` | — | Disc filesystem driver (`cdrom0:`). |
| `poweroff` | — | IOP power-off driver. |

```js
// Debug (node tools/modules.js configure --modules=debug,usbmass): on screen,
// since the console has no terminal.
Debug.overlay(true);                          // FPS, CPU, RAM, VRAM, frame graph
Debug.console(true, { lines: 5 });            // the last lines of console.log
Debug.watch("player", () => `${player.x | 0},${player.y | 0}`);
Debug.toggleWith(Gamepad.L3 | Gamepad.R3);    // show / hide it all

// Hitboxes of many entities: one call per frame, 4 floats per box.
Debug.rects(boxes, Color.new(80, 220, 120));  // Float32Array of x, y, w, h
Debug.rect(x, y, 16, 16, Color.new(255, 0, 0), { seconds: 0.5 });   // one, for half a second
```

The overlay shows its own cost (`debug x ms`, about 1.1 ms per frame on the PS2
with the console); `{ compact: true }` keeps only the FPS line and the watches, and
`{ heap: true }` adds the JavaScript heap, read every 5 s since it walks the heap. Everything is drawn after the game's
`draw`, and an error inside it turns the module off instead of the game.

USB, MX4SIO, the internal HDD and i.LINK all appear as `mass:/` through the
same BDM drivers. When booting from `mass:`, the drivers of every one of
these modules in the build are started, the one holding the boot folder is
found, and only that one is kept.

Enable `profiler` to measure named sections of a frame:

```js
Profiler.auto();                         // close history frames after each draw
Profiler.overlay(true);                 // scope statistics in the Debug overlay
const aiScope = Profiler.scope("ai");
// Inside update():
Profiler.measure(aiScope, () => updateAI());
Profiler.count("enemies.active", enemies.length);
```

`stats(scope)` reports milliseconds for timers and totals for counters.
Scopes are inclusive, so nested time is counted in the parent too.
`Profiler.attachRender3D(Render3D)` adds rendering counters and reports
CPU clipping; include `render3d` explicitly to use it. Manual loops call
`Profiler.frame()` once per frame. Individual timer spans must be shorter
than about 14 seconds because the EE counter wraps.

Enable `bench` with `node tools/modules.js configure --modules=bench,usbmass`,
adding the modules used by your tasks. For repeatable measurements,
`Bench.run()` attaches to an existing Loop and runs one synchronous batch
per frame:

```js
Bench.run([
    { name: "empty", run() {} },
    { name: "navigation", run() { crowd.update(1 / 60); } },
], {
    warmup: 30, samples: 120, iterations: 1,
    metadata: { platform: "PS2", revision: "record-your-build-commit", timing: "CPU" },
    path: "results.json",                         // writable boot device
}).then(report => { console.log(JSON.stringify(report)); Loop.stop(); });
Loop.run(() => {});
```

Tasks can provide `setup()` and `teardown(context)`; cleanup runs even if a
task fails, and the next task continues. Task failures appear in the report;
checkpoint failures reject the promise. Warmup batches, setup, cleanup and
JSON writes are excluded from timings. Reported milliseconds are per
invocation (batch duration divided by `iterations`), including JS and timer
overhead. `Bench.Runner.step()` supports manual loops and `cancel()` stops
the runner. Every batch must finish before the EE clock wraps (about 14.5 s).
Drawing measures CPU submission unless the task explicitly waits for the
GS. Use an empty task as a timing reference and record platform, build and
scene metadata; compare repeated runs of the same workload.
[bin/world_systems_bench.js](bin/world_systems_bench.js) measures LOD,
navigation and triggers and writes a checkpoint after each task.

```js
// Read a file straight from a zip, without extracting it.
const pack = Archive.open("assets.zip");
const bytes = new Uint8Array(Archive.read(pack, "levels/1.json"));
Archive.close(pack);
```

```js
// Save and load a game. The *Async calls run on a worker thread, so the frame
// loop keeps drawing; atomic saves keep the previous save if the card is pulled.
const SAVE = "mc0:/MYGAME/save.json";

async function save(state) {
    try {
        await MemoryCard.writeJSONAsync(SAVE, state, { atomic: true });  // creates mc0:/MYGAME
    } catch (error) {
        console.log(`Save failed: ${error.code}`);                      // NO_CARD, FULL, CARD_CHANGED...
    }
}

async function load() {
    if (!MemoryCard.getInfo(0).connected) return null;
    return MemoryCard.exists(SAVE) ? await MemoryCard.readJSONAsync(SAVE) : null;
}

// The PS2 browser shows a save directory that has an icon.sys and its icon.
MemoryCard.writeFile("mc0:/MYGAME/icon.sys",
    MemoryCard.createIconSys({ title: "My Game\nSlot 1", icon: "icon.ico" }));
```

Three files can be open at once on both cards together, `fopen("mc0:...")`
included, and each directory holds a fixed number of entries. A handle opened
before the card was swapped or the IOP was reset is refused instead of writing
to the wrong card. `bin/tests/memcard_example.js` is a complete save/load
screen.

For game save slots, `SaveGame` adds validation and version migrations over
the lower-level MemoryCard API:

```js
SaveGame.define({
    directory: "MYGAME", title: "My Game", icon: "assets/icon.ico", version: 2,
    migrate(data, fromVersion) {
        return fromVersion === 1 ? { ...data, coins: 0 } : data;
    },
});
async function saveProgress() {
    await SaveGame.save(0, { level: 3, coins: 120 });
    const state = await SaveGame.load(0);    // null for an empty slot
    return state;
}
```

Slots are 0–99 or names of 1–20 letters, digits, underscores or hyphens.
`save()` and `load()` are asynchronous; `exists()`, `list()` and `status()`
are synchronous card accesses. Catch errors by `error.code`, including
MemoryCard errors, `CORRUPT`, `NEWER_VERSION` and `OLD_VERSION` (a migration
is required). Include `archive` for gzip compression; without an icon,
SaveGame does not create `icon.sys` for the PS2 browser.

### Concurrency and timing

| Module | Global | Description |
|---|---|---|
| [`thread`](src/modules/thread/thread.d.ts) | `Thread` | EE threads with stack and priority control, recursion limit protection, and the worker pool behind background jobs. |
| [`mutex`](src/modules/mutex/mutex.d.ts) | `Mutex` | Mutual exclusion for data shared between threads. |
| [`timer`](src/modules/timer/timer.d.ts) | `Timer` | Pausable elapsed-time timers. |

```js
const mutex = Mutex.new();
let counter = 0;

const worker = Thread.new(() => {
    Mutex.lock(mutex);
    counter++;
    Mutex.unlock(mutex);
}, "worker", 32768);

Thread.start(worker);
```

Callbacks execute preemptively on native EE threads and share the QuickJS
runtime through an internal lock (the GIL); deeper recursion throws a
catchable `InternalError: stack overflow` instead of overrunning the stack
(`bin/tests/stack_test.js`). For I/O and heavy decoding, prefer the
[Background jobs](#background-jobs) pool (`*Async`).

### Native modules

| Module | Global | Description |
|---|---|---|
| `erl` | — | Loads relocatable native modules (`.erl`) at runtime. Not in the default build; see [Native code](#native-code). |

## Configuration

`athena.ini`, next to `athena.elf`, is read at boot:

```ini
# Script to run (default: main.js)
default_script=main.js

# IOP drivers to start at boot, by name (see IOP.getModules())
audsrv = true
```

Command-line arguments, for launchers that pass them, override it:

| Argument | Effect |
|---|---|
| `--script=<path>`, `-s=<path>` | Run another script. |
| `--cfg=<path>`, `-c=<path>` | Read another configuration file. |
| `--ignorecfg`, `-i` | Do not read the configuration file. |
| `--noiopreset`, `-n` | Keep the IOP drivers loaded by the launcher. |

When booted from a disc, the configuration file is `cdrom0:ATHENA.INI;1`.

## Building from source

The simplest way is Docker, which provides the PS2 toolchain:

```shell
docker compose run --rm build   # bin/athena.elf (and bin/athena_pkd.elf when ps2-packer is available)
docker compose run --rm debug   # bin/athena_debug.elf
```

### Choosing modules

Only the selected modules, their dependencies and the required ones are
compiled and linked:

```shell
node tools/modules.js list                                     # available modules
node tools/modules.js configure --defaults                     # the default build
node tools/modules.js configure --modules=screen,loop,gamepad   # a custom selection
node tools/modules.js configure --all                          # everything
```

`configure` regenerates `Makefile.modules`, `src/generated/` and
`bin/athena.d.ts`. A custom selection replaces the previous selection;
dependencies and required modules are added automatically, but the default
modules are not. Use lowercase module ids in this command and the exported
names (such as `Scene3D` or `Audio3D`) in JavaScript. Rebuild the ELF after
configuring; changing the declarations alone does not enable a module.

For example, a USB build for the procedural world example is:

```shell
node tools/modules.js configure --modules=screen,loop,meshbuilder,lod,nav,triggers3d,sky,debug3d,usbmass
docker compose run --rm build
# Set default_script=world_systems_example.js in bin/athena.ini.
```

Boot devices are modules too: a build that runs from USB can
drop `memcard` and `cdrom` to save RAM, while builds booting from SD card
adapters or internal storage include `mx4sio`, `hdd` or `ilink`. A build without
the driver of its boot device cannot read its own scripts.

Local builds with a ps2dev toolchain, build options and the C library are
described in [docs/BUILDING_ATHENA.md](docs/BUILDING_ATHENA.md).

The module picker in [public/index.html](public/index.html) shows the catalog
and generates configuration commands. An optional build server can produce
selected QuickJS builds or native SDKs; see
[docs/BUILD_SERVER.md](docs/BUILD_SERVER.md) for its dedicated checkout,
configuration and API. `node tools/modules.js catalog` updates the published
catalogs and combined declarations for all available modules; these differ
from `bin/athena.d.ts`, which describes only the configured build.

### Adding a module

A module is a folder in `src/modules/<id>/`:

```
src/modules/<id>/
├── module.json        # id, description, dependencies, sources, QuickJS binding
├── include/athena/    # public C API
├── native/            # C implementation, independent of JavaScript
├── quickjs/           # JavaScript binding
├── js/                # JavaScript implementation, for modules written in JavaScript
└── <id>.d.ts          # TypeScript declaration
```

The native part never depends on QuickJS and is usable from C. Modules
implemented entirely in JavaScript have no C API; hybrid modules expose
their native functionality through public headers, while their JavaScript
helpers require the QuickJS runtime.

#### JavaScript modules

Code that organizes a game (scenes, menus, tweens) gains nothing from C. A
module can be written in JavaScript instead, and is embedded in the binary,
selected, typed and made global like any other:

```json
{
  "id": "scene",
  "name": "Scene",
  "dependencies": { "modules": ["loop", "imagelist"] },
  "js": { "source": "js/scene.js", "module_name": "Scene", "global_alias": "Scene" },
  "types": "scene.d.ts"
}
```

- `source` is one ES module file. It is embedded as text and compiled the
  first time it is imported, so its errors report `Scene:<line>`.
- Import what it uses (`import * as Loop from "Loop"`): the globals of other
  modules are assigned only after every module has been evaluated. Relative
  imports are resolved on the boot device, not in the binary.
- `global_export` works as for QuickJS bindings, e.g. `"Scene.Scene"` to make
  a class the global.
- A module may have both a QuickJS binding and a JavaScript part, with
  different module names: the fast path in C, the rest in JavaScript. JavaScript
  modules only exist with `RUNTIME=quickjs`; C code never depends on them.

## Native code

### Native runtime

AthenaEnv can be built without the JavaScript engine. Your C code implements
`int athena_main(int argc, char **argv)` and uses the modules through their C
headers; boot, IOP drivers and exception handling work as in the JavaScript
runtime.

```shell
make RUNTIME=native APP_SRCS=samples/native/hello/main.c   # bin/athena_native.elf
make lib RUNTIME=native                                    # lib/libathena.a for other projects
make sdk RUNTIME=native                                    # dist/athena-sdk.tar.gz: library, headers and samples
```

See `samples/native/` (`camera/` follows a square with the Camera2D C API)
and [docs/BUILDING_ATHENA.md](docs/BUILDING_ATHENA.md).

### ERL modules

With the `erl` module in the build, scripts can load native code compiled as a
relocatable `.erl` module:

```js
import * as CustomModule from "custom_module.erl";
console.log(CustomModule.fibonacci(20));
```

`native_module_template/` contains a buildable example. The `erl` module
exports every symbol of the binary, which costs RAM and disables dead-code
removal, so it is off by default.

## Testing

| Command | What it runs |
|---|---|
| `docker compose run --rm host-tests` | C tests of the runtime and modules on the build machine (`tests/host/`). |
| `docker compose run --rm js-tests` | Module test scripts under AddressSanitizer (`tests/js/`), plus the 3D and particle C tests (`tests/host/run_3d.sh`). |
| `sh tools/build_3d.sh` (in the build image) | 3D sample ELFs and profiles; then the host tests. |

JavaScript modules (`src/modules/<name>/js/`) are loaded directly from their
source by the test runner (`tests/js/runner.c`), using a JavaScript stub for
`Loop` (`tests/js/stub/Loop.js`) because the real `Loop` requires GS
initialization.

`bin/tests/` holds test scripts and examples to run on PCSX2 or a PS2.
`bin/tests/index.js` is a launcher for them, and `bin/athena.ini` starts it
(`default_script=tests/index.js`), so switching tests needs no edit:

- **✕** runs the selected script, **UP/DOWN** move, **LEFT/RIGHT** turn a page.
- A script returns to the launcher when it ends, when it throws, or when
  **SELECT+START** is held for a second on the pad in port 1. The shortcut
  works in every script, including `while (true)` loops and scripts that
  never read the pad.
- Back in the launcher, what the script printed (`console.log()`, `print()`,
  unhandled promise rejections) and its error with the stack trace are shown
  on screen, scrolled to the end where test summaries are: failures in red,
  passes in green. **UP/DOWN** scroll, **LEFT/RIGHT** turn a page, **✕** or
  **○** go back to the list, and **△** shows it again. The last 16 KiB are
  kept; the same output still goes to the EE console (the PCSX2 log).

Scripts run one after another in the same ELF, so native state that outlives
a script (loaded IOP drivers, enabled gamepad drivers) carries over.
Tests that check a fresh start, such as `gamepad_test.js`, are only reliable
as the first script after boot. Key test suites in `bin/tests/` include
`stack_test.js` (recursion and stack limits), `font_async_test.js` (font
rasterization and async jobs), `memcard_test.js` (synchronous and async saves)
and `memory_stats.js`.

Additional suites cover the optional modules: `input_test.js`,
`replay_test.js`, `savegame_test.js`, `assets3d_test.js`, `profiler_test.js`,
`debug3d_test.js`, `meshbuilder_test.js`, `voxel_test.js`, `lod_test.js`,
`nav_test.js`, `triggers3d_test.js`, `audio3d_test.js`, `sky_test.js` and
`bench_test.js`.
Include the modules a suite uses when running it on the PS2. Host runners
stub hardware services, so they check APIs and algorithms; GS output, SPU2
playback and device behavior still need emulator or console verification.

`soak_test.js` checks that switching scripts does not leak or hang: it runs
the heavy scripts of `bin/tests/soak/` (fonts, sound, ImageList, video, jobs,
threads) six times in a row, each one switching back with `std.reload()` in
the middle of its work, and compares the EE heap and VRAM at the start of
every round. It keeps its progress in `bin/tests/soak/state.json`, so run it
from a writable device (USB or `host:`), and reports in the launcher when
done.

Test on real hardware before a release: the emulator tolerates misaligned
memory accesses and provides the `host:` device, which a console does not.

## Contributing

Contributions are welcome.

1. Fork the project.
2. Create a branch (`git checkout -b feature/my-feature`).
3. Commit your changes, with tests for new behavior.
4. Push the branch and open a pull request.

## License

Distributed under the MIT License. See [LICENSE](LICENSE).

## Credits

AthenaEnv was created by [Daniel Santos](https://github.com/DanielSant0s).

### Built with

- [ps2dev](https://github.com/ps2dev/ps2dev) and [gsKit](https://github.com/ps2dev/gsKit)
- [QuickJS](https://bellard.org/quickjs/)
- [Box2D](https://box2d.org)
- [zlib](https://zlib.net) and minizip
- [FreeType](https://freetype.org)

### Thanks

Direct and indirect thanks to the people who made the project viable:

- guiprav - for 3dcb-duktape, which was the inspiration for bringing Enceladus and JavaScript together
- HowlingWolf&Chelsea - tests, tips and many other things
- The whole PS2DEV team
- Erin Catto - for Box2D, the physics engine behind the `box2d` module
