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
hardware through native modules: GS rendering, VU1-accelerated tilemaps, IPU
video decoding, SPU2 audio, controllers, storage, threads and physics.

Every feature is a **module** that can be left out of the build. A game ships
only what it uses, so the binary and its RAM footprint stay small. The same
modules can also be used from C, without the JavaScript engine.

Highlights:

- **Game loop with native timing**: `Loop.run()` provides the frame delta,
  fixed-step updates for physics, time scaling and frame statistics.
- **2D rendering**: primitives, textured images, TrueType and bitmap fonts,
  and a VU1-batched tilemap renderer for thousands of sprites.
- **Asynchronous assets**: images, sound effects and archives load on worker
  threads without stalling the frame.
- **Memory card saves**: JSON saves that survive a pulled card, awaitable
  without dropping frames, and `icon.sys` generation for the PS2 browser.
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

Every module of the build is available as a global (`Screen`, `Draw`,
`Gamepad`...). Modules can also be imported explicitly, which documents the
dependencies of a file:

```js
import * as Screen from "Screen";
import { Font } from "Font";
```

`Font`, `Image`, `ImageList` and `Video` are classes; the other modules are
namespaces of functions and constants. QuickJS's `std` and `os` modules are
global as well, together with `setTimeout`, `setInterval`, `setImmediate` and
their `clear*` counterparts.

Your own files are regular ES modules:

```js
// utils.js
export const clamp = (value, min, max) => Math.min(Math.max(value, min), max);

// main.js
import { clamp } from "utils.js";
```

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

## Modules

The default build contains every module except `box2d`, `box2ddraw` and `erl`.
Each module's API is documented in its TypeScript declaration, linked in the
tables below.

### Graphics

| Module | Global | Description |
|---|---|---|
| [`screen`](src/modules/screen/screen.d.ts) | `Screen` | Video modes, clear and flip, VSync, FPS, VRAM statistics and GS state (alpha, depth, scissor). |
| [`loop`](src/modules/loop/loop.d.ts) | `Loop` | Runtime-driven game loop: frame delta, fixed steps, time scale, frame pacing and statistics. |
| [`draw`](src/modules/draw/draw.d.ts) | `Draw` | Points, lines, rectangles, circles, triangles and quads, flat or Gouraud shaded. |
| [`color`](src/modules/color/color.d.ts) | `Color` | Packing and editing of RGBA colors. |
| [`image`](src/modules/image/image.d.ts) | `Image` | PNG, JPEG and BMP loading, pixel access and textured drawing with tint, source rectangle and rotation. |
| [`imagelist`](src/modules/imagelist/imagelist.d.ts) | `ImageList` | Asynchronous image loading with priorities, deduplication, an LRU cache and an optional decoder thread. |
| [`font`](src/modules/font/font.d.ts) | `Font` | TrueType and bitmap fonts with scaling, alignment, outline and drop shadow. |
| [`tilemap`](src/modules/tilemap/tilemap.d.ts) | `TileMap` | VU1-accelerated batched sprites and tilemaps. |
| [`video`](src/modules/video/video.d.ts) | `Video` | MPEG-1/2 playback on the IPU, drawn directly or used as an `Image`. See [docs/VIDEO.md](docs/VIDEO.md). |
| `graphics` | — | GS initialization and the rendering core shared by the modules above. |

```js
const logo = new Image("logo.png");
const font = new Font("fonts/title.ttf");
font.scale = 1.5;

Loop.run(() => {
    logo.draw(100, 80);
    font.print(100, 300, "Press START");
}, { clearColor: Color.new(20, 20, 40) });
```

### Input

| Module | Global | Description |
|---|---|---|
| [`gamepad`](src/modules/gamepad/gamepad.d.ts) | `Gamepad` | Up to eight players: DualShock 2 on both ports and multitaps, DualShock 3/4 over USB and Bluetooth. Buttons, sticks, pressure and rumble. |

```js
Gamepad.configure({ multitap: true });   // optional drivers: multitap, usb, bluetooth
const pad = Gamepad.player(0);

Loop.run(() => {
    Gamepad.update();                    // once per frame
    if (pad.justPressed(Gamepad.CROSS)) pad.rumble(0.5, 0, 200);
});
```

### Audio

| Module | Global | Description |
|---|---|---|
| [`sound`](src/modules/sound/sound.d.ts) | `Sound` | ADPCM sound effects on the 24 SPU2 voices and one streamed WAV or Ogg Vorbis music track, with fades. |

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

### Physics

Not in the default build; see [docs/BOX2D.md](docs/BOX2D.md).

| Module | Global | Description |
|---|---|---|
| [`box2d`](src/modules/box2d/box2d.d.ts) | `Box2D` | Box2D 3.2: worlds, bodies, five shape types, chains, seven joint types, ray and shape casts, overlap queries, character movers, events and snapshots. |
| [`box2ddraw`](src/modules/box2ddraw/box2ddraw.d.ts) | `Box2DDraw` | Debug drawing of a Box2D world. |

### Math

| Module | Global | Description |
|---|---|---|
| [`vector`](src/modules/vector/vector.d.ts) | `Vector` | `Vector2`, `Vector3` and `Vector4` with PS2 alignment. |
| [`matrix4`](src/modules/matrix4/matrix4.d.ts) | `Matrix4` | 4×4 transformation matrices. |

### System and storage

| Module | Global | Description |
|---|---|---|
| [`system`](src/modules/system/system.d.ts) | `System` | Files and folders, devices, hardware information, memory statistics, timing, garbage collection and launching ELFs. Always included. |
| [`archive`](src/modules/archive/archive.d.ts) | `Archive` | Reads zip, tar, tar.gz and gzip; safe extraction, also on a worker thread; gzip in memory. |
| [`iop`](src/modules/iop/iop.d.ts) | `IOP` | IOP driver discovery, loading, reset and memory statistics. |
| [`memcard`](src/modules/memcard/memcard.d.ts) | `MemoryCard` | Memory cards on `mc0:/` and `mc1:/`: card status and swap detection, files (whole, JSON or streamed), directories, attributes and dates, atomic saves, `icon.sys`, format, and every slow call also as an awaitable background job. Also the drivers the memory card boot device needs. |
| `usbmass` | — | USB storage drivers (`mass:/`). |
| `cdrom` | — | Disc filesystem driver (`cdrom0:`). |
| `poweroff` | — | IOP power-off driver. |

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

### Concurrency and timing

| Module | Global | Description |
|---|---|---|
| [`thread`](src/modules/thread/thread.d.ts) | `Thread` | EE threads. |
| [`mutex`](src/modules/mutex/mutex.d.ts) | `Mutex` | Mutual exclusion for data shared with threads. |
| [`timer`](src/modules/timer/timer.d.ts) | `Timer` | Pausable elapsed-time timers. |

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
`bin/athena.d.ts`. Boot devices are modules too: a build that runs from USB can
drop `memcard` and `cdrom` to save RAM, but a build without the driver of its
boot device cannot read its own scripts.

Local builds with a ps2dev toolchain, build options and the C library are
described in [docs/BUILDING_ATHENA.md](docs/BUILDING_ATHENA.md).

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

The native part never depends on QuickJS, so every module is usable from C.

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
```

See `samples/native/` and [docs/BUILDING_ATHENA.md](docs/BUILDING_ATHENA.md).

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
| `docker compose run --rm js-tests` | Module test scripts under AddressSanitizer (`tests/js/`). |

`bin/tests/` holds test scripts and examples to run on PCSX2 or a PS2: set
`default_script=tests/<name>.js` in `athena.ini`. Test on real hardware
before a release: the emulator tolerates misaligned memory accesses and
provides the `host:` device, which a console does not.

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
