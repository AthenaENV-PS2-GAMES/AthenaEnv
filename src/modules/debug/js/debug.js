/*
 * Debug: on-screen diagnostics for the console, where there is no terminal.
 *
 *   - overlay: FPS, CPU and frame time, memory, VRAM, watches, its own cost
 *     and a frame-time graph (samples and bars in C, DebugNative);
 *   - console: the last lines the script printed, read from the runtime's
 *     output buffer (the same one std.lastRun() shows in the launcher);
 *   - shapes and text that stay for a while, in screen or world space
 *     (rects, lines and circles are queued and drawn in C: one hitbox per
 *     entity per frame allocates nothing);
 *   - a controller shortcut that shows and hides everything.
 *
 * Everything draws in a Loop system (postDraw, after the game's draw),
 * registered only while something is on. Games with their own loop call
 * Debug.frame(dt) after drawing instead.
 *
 * Cost: the overlay text is rebuilt 4 times per second and the console text
 * when the script prints; both are laid out once (Font.render) and only
 * drawn each frame. A debugging aid must not take the game down: an error
 * while drawing turns the module off and is logged once.
 */
import * as Loop from "Loop";
import * as Draw from "Draw";
import * as Screen from "Screen";
import * as System from "System";
import * as Color from "Color";
import { Font } from "Font";
import * as Native from "DebugNative";

const REFRESH_SECONDS = 0.25;
/*
 * RAM comes from counters (System.getUsedMemory/getFreeMemory), read with
 * the rest of the text. The JavaScript heap line needs
 * System.getMemoryStats(), which walks the whole heap (JS_ComputeMemoryUsage)
 * to count objects: 6.7 ms per call measured on the PS2, in a single frame.
 * It is off by default (the overlay would drop a frame each time in a busy
 * game) and, with `heap: true`, read every 5 seconds.
 */
const HEAP_SECONDS = 5;
const STATS_FRAMES = 60;
const PADDING = 4;
const GRAPH_WIDTH = 200;
const GRAPH_HEIGHT = 36;
const FONT_SIZE = 16;
/* Title-safe area: CRT TVs hide up to 5-10% of each edge (overscan). */
const SAFE_AREA = 0.05;
const MAX_TEXTS = 2048;
/* DebugNative.shape() kinds and flags (<athena/debug_overlay.h>). */
const RECT = 0, LINE = 1, CIRCLE = 2;
const WORLD = 1, FILLED = 2;
const WATCH_CHARS = 48;
const MB = 1024 * 1024;

const PANEL = Color.new(0, 0, 0, 80);          // 0x80 is opaque: this is see-through
const TEXT = Color.new(255, 255, 255, 128);
const ERROR_TEXT = Color.new(255, 110, 100, 128);
const SHAPE = Color.new(255, 60, 60, 128);
/* Console lines drawn in ERROR_TEXT, as the launcher colors failures. */
const ERROR_LINE = /\b(error|uncaught|fail(ed)?|exception)\b|\[FAIL\]/i;

const state = {
    shown: true,            // flipped by the shortcut; data keeps flowing
    overlay: false,
    graph: true,            // the frame-time graph under the overlay text
    compact: false,         // overlay text: the FPS line and the watches only
    heap: false,            // JS heap line (walks the heap: every HEAP_SECONDS)
    console: false,
    consoleLines: 8,
    budgetMs: 0,            // 0: from the video mode and vsyncInterval
    vsyncInterval: 1,
    margin: null,           // null: the safe area of the video mode
    font: null,
    watches: new Map(),
    shapeCount: 0,          // native queue, as of the last draw (plus new ones)
    texts: [],
    textsDropped: 0,        // texts discarded over MAX_TEXTS
    view: { x: 0, y: 0, scale: 1 },
    toggle: null,           // { buttons, port, held }
    system: null,
    warnedFrame: false,
    // Cached per refresh (4 Hz), not read every frame.
    refreshIn: 0,
    heapIn: 0,
    heapLine: "",
    mode: null,
    charWidth: 0,
    selfMs: 0,
    overlayRender: null,
    overlaySize: { width: 0, height: 0 },
    consoleRenders: [],
    consoleVersion: -1,
    consoleColumns: -1,
};

function font() {
    if (!state.font) {
        state.font = new Font({ size: FONT_SIZE });
        // Rasterize the glyphs ahead, a millisecond per frame, instead of
        // holding the first frame the overlay shows.
        if (typeof state.font.preload === "function")
            state.font.preload(undefined, { budgetMs: 1 }).catch(() => {});
        invalidateText();
    }
    return state.font;
}

/* Inside this module `console` is Debug.console: log through the global one. */
function log(text) {
    globalThis.console.log(text);
}

function invalidateText() {
    state.refreshIn = 0;
    state.overlayRender = null;
    state.consoleVersion = -1;
    state.charWidth = 0;
}

function checkNumber(value, name) {
    if (typeof value !== "number" || !Number.isFinite(value))
        throw new TypeError(`${name} must be a finite number`);
    return value;
}

/* Packed colors (Color.new) are uint32; int32 packings are accepted too. */
function checkColor(color, fallback, name) {
    if (color === undefined) return fallback;
    if (typeof color !== "number" || !Number.isInteger(color) ||
        color < -0x80000000 || color > 0xFFFFFFFF)
        throw new TypeError(`${name} color must be a Color.new() value`);
    return color;
}

// --- The Loop system: on while anything needs a frame --------------------------

function busy() {
    return state.overlay || state.console || state.shapeCount > 0 || state.texts.length > 0 ||
        state.toggle !== null;
}

function useSystem() {
    if (state.system || !busy()) return;
    state.system = Loop.addSystem({
        name: "debug",
        priority: 1000,         // after every other system: drawn on top
        realTime: true,         // shapes last real seconds, also while paused
        preUpdate(realDt) { age(realDt); },
        postDraw() {
            render(Loop.getStats());
            releaseSystem();
        },
    });
}

function releaseSystem() {
    if (state.system && !busy()) {
        Loop.removeSystem(state.system);
        state.system = null;
    }
}

function age(dt) {
    Native.shapesAge(dt);
    for (const text of state.texts) text.remaining -= dt;
    state.refreshIn -= dt;
    state.heapIn -= dt;
}

/*
 * One frame: samples, shortcut, then drawing. Never throws: an error (a freed
 * font, a bad value reaching a draw call) turns the module off, logs once and
 * lets the game go on.
 */
function render(stats) {
    const start = System.getMilliseconds();
    try {
        Native.record(stats.frameMs, stats.cpuMs);
        pollToggle();
        if ((state.overlay || state.console) && state.refreshIn <= 0) refresh(stats);
        // Drawn, or only expired while hidden.
        const view = state.view;
        state.shapeCount = Native.shapesDraw(view.x, view.y, view.scale, state.shown);
        if (state.shown) {
            drawTexts();
            if (state.overlay) drawOverlay();
            if (state.console) drawConsole();
        }
    } catch (error) {
        shutDown(error);
        return;
    }
    // One-frame texts, and the ones whose time is up, go after being drawn.
    let kept = 0;
    for (const text of state.texts)
        if (text.remaining > 0) state.texts[kept++] = text;
    state.texts.length = kept;
    // Smoothed, so the figure is readable at 4 refreshes per second.
    state.selfMs += (System.getMilliseconds() - start - state.selfMs) * 0.1;
}

function shutDown(error) {
    state.overlay = false;
    state.console = false;
    Native.shapesClear();
    state.shapeCount = 0;
    state.texts.length = 0;
    state.toggle = null;
    // The error may come from the font (a configured one that was freed):
    // turning the module on again starts from the built-in one.
    state.font = null;
    invalidateText();
    releaseSystem();
    log(`Debug: turned off after an error while drawing: ${error}` +
        (error && error.stack ? `\n${error.stack}` : ""));
}

function pollToggle() {
    const toggle = state.toggle;
    if (!toggle) return;
    const held = (Native.pad(toggle.port) & toggle.buttons) === toggle.buttons;
    if (held && !toggle.held) state.shown = !state.shown;
    toggle.held = held;
}

// --- Cached per refresh ----------------------------------------------------------

function isFiftyHertz(mode) {
    return mode === Screen.PAL || mode === Screen.DTV_576p;
}

/* The frame budget of the graph colors, in milliseconds. */
function budget() {
    if (state.budgetMs > 0) return state.budgetMs;
    const refresh = state.mode && isFiftyHertz(state.mode.mode) ? 50 : 60;
    return 1000 * state.vsyncInterval / refresh;
}

function margins() {
    if (state.margin) return state.margin;
    return {
        x: Math.round(state.mode.width * SAFE_AREA),
        y: Math.round(state.mode.height * SAFE_AREA),
    };
}

function megabytes(bytes) {
    return (bytes / MB).toFixed(1);
}

function clip(text) {
    // ASCII dots: the built-in font may have no ellipsis glyph.
    return text.length > WATCH_CHARS ? text.slice(0, WATCH_CHARS - 3) + "..." : text;
}

function refresh(stats) {
    state.refreshIn = REFRESH_SECONDS;
    state.mode = Screen.getMode();
    if (!state.charWidth) state.charWidth = Math.max(1, font().getTextSize("M").width);
    if (state.overlay) buildOverlay(stats);
}

function buildOverlay(stats) {
    const frames = Native.stats(STATS_FRAMES);
    const fpsLine =
        `${stats.fps.toFixed(1)} FPS  cpu ${frames.cpuAvg.toFixed(1)} ms (max ${frames.cpuMax.toFixed(1)})`;
    if (state.heap && !state.compact && (state.heapIn <= 0 || !state.heapLine)) {
        const memory = System.getMemoryStats();
        state.heapLine = `JS ${megabytes(memory.jsHeap)}/${megabytes(memory.jsLimit)} MB, ` +
            `${memory.jsObjects} objects (every ${HEAP_SECONDS} s)`;
        state.heapIn = HEAP_SECONDS;
    }
    const lines = state.compact ? [fpsLine] : [
        fpsLine,
        `frame ${frames.frameAvg.toFixed(1)} ms (max ${frames.frameMax.toFixed(1)})  budget ${budget().toFixed(1)}`,
        `RAM ${megabytes(System.getUsedMemory())} MB used, ${megabytes(System.getFreeMemory())} MB free`,
        `VRAM ${(Screen.getFreeVRAM() / 1024).toFixed(0)} KB free  debug ${state.selfMs.toFixed(2)} ms`,
    ];
    if (state.heap && !state.compact) lines.splice(3, 0, state.heapLine);
    const dropped = Native.shapesDropped() + state.textsDropped;
    if (dropped) lines.push(`shapes dropped: ${dropped} (queue full)`);
    for (const [name, read] of state.watches) {
        let value;
        try {
            value = String(read());
        } catch (error) {
            value = `<${error && error.message ? error.message : error}>`;
        }
        lines.push(clip(`${name}: ${value}`));
    }
    const f = font();
    const text = lines.join("\n");
    state.overlayRender = f.render(text);
    state.overlaySize = f.getTextSize(text);
}

// --- Overlay -------------------------------------------------------------------

function drawOverlay() {
    const f = font();
    if (!state.overlayRender) buildOverlay(Loop.getStats());
    const { x, y } = margins();
    const textHeight = state.overlaySize.height;
    const width = (state.graph ? Math.max(state.overlaySize.width, GRAPH_WIDTH) :
        state.overlaySize.width) + 2 * PADDING;
    const height = textHeight + (state.graph ? GRAPH_HEIGHT + PADDING : 0) + 2 * PADDING;
    Draw.rect(x, y, width, height, PANEL);
    printRender(state.overlayRender, x + PADDING, y + PADDING, TEXT, f);
    if (state.graph) {
        const graphBudget = budget();
        Native.graph(x + PADDING, y + 2 * PADDING + textHeight, width - 2 * PADDING, GRAPH_HEIGHT,
            graphBudget, graphBudget * 2);
    }
}

// --- Console -------------------------------------------------------------------

function drawConsole() {
    const f = font();
    const { x: left, y: bottom } = margins();
    const width = state.mode.width - 2 * left;
    // A proportional font: "M" is about the widest, so lines never overflow.
    const columns = Math.max(1, Math.floor((width - 2 * PADDING) / state.charWidth));
    const version = Native.outputVersion();
    if (version !== state.consoleVersion || columns !== state.consoleColumns) {
        const text = Native.output(state.consoleLines, columns);
        state.consoleRenders = text ? text.split("\n").map(line =>
            ({ render: f.render(line), color: ERROR_LINE.test(line) ? ERROR_TEXT : TEXT })) : [];
        state.consoleVersion = version;
        state.consoleColumns = columns;
    }
    const lineHeight = f.lineHeight;
    const lines = Math.min(state.consoleLines,
        Math.max(1, Math.floor((state.mode.height - 2 * bottom - 2 * PADDING) / lineHeight)));
    const height = lines * lineHeight + 2 * PADDING;
    const top = state.mode.height - bottom - height;
    Draw.rect(left, top, width, height, PANEL);
    const shown = state.consoleRenders.slice(-lines);
    for (let i = 0; i < shown.length; i++)
        printRender(shown[i].render, left + PADDING, top + PADDING + i * lineHeight, shown[i].color, f);
}

// --- Shapes --------------------------------------------------------------------

/* A FontRender drawn in `color`: it prints with the font's current color. */
function printRender(render, x, y, color, f) {
    const previous = f.color;
    f.color = color;
    render.print(x, y);
    f.color = previous;
}

function drawTexts() {
    const view = state.view, f = font();
    for (const text of state.texts) {
        const world = text.world;
        const x = world ? (text.x - view.x) * view.scale : text.x;
        const y = world ? (text.y - view.y) * view.scale : text.y;
        const previous = f.color;
        f.color = text.color;
        f.print(x, y, text.value);
        f.color = previous;
    }
}

/*
 * Options of a shape into shapeSeconds and shapeFlags: module variables, so
 * a shape per entity per frame allocates nothing.
 */
let shapeSeconds = 0, shapeFlags = 0;

function shapeOptions(options, name) {
    shapeSeconds = 0;
    shapeFlags = 0;
    if (options === undefined) return;
    if (options === null || typeof options !== "object")
        throw new TypeError(`${name} options must be an object`);
    if (options.seconds !== undefined) shapeSeconds = checkNumber(options.seconds, `${name} seconds`);
    const space = options.space;
    if (space === "world") shapeFlags |= WORLD;
    else if (space !== undefined && space !== "screen")
        throw new TypeError(`${name} space must be "screen" or "world"`);
    if (options.filled) shapeFlags |= FILLED;
}

function queueShape(kind, x, y, a, b, color, options, name) {
    shapeOptions(options, name);
    Native.shape(kind, x, y, a, b, color, shapeSeconds, shapeFlags);
    state.shapeCount++;
    useSystem();
}

// --- API -----------------------------------------------------------------------

/**
 * Shows or hides the stats panel; returns whether it is on. Options:
 * `graph` (default true) draws the frame-time graph; `compact` (default
 * false) keeps only the FPS line and the watches; `heap` (default false)
 * adds the JavaScript heap and object count, read every 5 seconds because
 * reading them walks the whole heap (6.7 ms measured on the PS2, in one
 * frame). Measured costs per frame: about 1.1 ms with the console, of which
 * the graph is about 0.45 ms (`graph: false` saves it).
 */
export function overlay(on, options) {
    if (options !== undefined) {
        if (options === null || typeof options !== "object")
            throw new TypeError("Debug.overlay options must be an object");
        if (options.graph !== undefined) state.graph = !!options.graph;
        if (options.compact !== undefined) state.compact = !!options.compact;
        if (options.heap !== undefined) {
            state.heap = !!options.heap;
            state.heapIn = 0;
        }
        state.overlayRender = null;
        state.refreshIn = 0;
    }
    if (on !== undefined) {
        state.overlay = !!on;
        state.refreshIn = 0;
        state.overlay ? useSystem() : releaseSystem();
    }
    return state.overlay;
}

/** Shows or hides the last lines of the script's output; returns whether it is on. */
export function console(on, options) {
    if (options !== undefined) {
        if (options === null || typeof options !== "object")
            throw new TypeError("Debug.console options must be an object");
        if (options.lines !== undefined) {
            const lines = checkNumber(options.lines, "Debug.console lines");
            if (lines < 1 || lines > 40 || lines !== Math.floor(lines))
                throw new RangeError("Debug.console lines must be an integer from 1 to 40");
            state.consoleLines = lines;
            state.consoleVersion = -1;
        }
    }
    if (on !== undefined) {
        state.console = !!on;
        state.consoleVersion = -1;
        state.refreshIn = 0;
        state.console ? useSystem() : releaseSystem();
    }
    return state.console;
}

/** Adds a line to the overlay: `name: read()`, refreshed 4 times per second. */
export function watch(name, read) {
    if (typeof name !== "string") throw new TypeError("Debug.watch name must be a string");
    if (typeof read !== "function") throw new TypeError("Debug.watch expects a function");
    state.watches.set(name, read);
    state.refreshIn = 0;
}

/** Removes a watch; returns whether it existed. */
export function unwatch(name) {
    state.refreshIn = 0;
    return state.watches.delete(name);
}

/** Outline (or `filled`) rectangle, for one frame or `seconds`. */
export function rect(x, y, width, height, color, options) {
    checkNumber(x, "Debug.rect x");
    checkNumber(y, "Debug.rect y");
    checkNumber(width, "Debug.rect width");
    checkNumber(height, "Debug.rect height");
    queueShape(RECT, x, y, width, height, checkColor(color, SHAPE, "Debug.rect"), options, "Debug.rect");
}

export function line(x1, y1, x2, y2, color, options) {
    checkNumber(x1, "Debug.line x1");
    checkNumber(y1, "Debug.line y1");
    checkNumber(x2, "Debug.line x2");
    checkNumber(y2, "Debug.line y2");
    queueShape(LINE, x1, y1, x2, y2, checkColor(color, SHAPE, "Debug.line"), options, "Debug.line");
}

export function circle(x, y, radius, color, options) {
    checkNumber(x, "Debug.circle x");
    checkNumber(y, "Debug.circle y");
    checkNumber(radius, "Debug.circle radius");
    queueShape(CIRCLE, x, y, radius, 0, checkColor(color, SHAPE, "Debug.circle"), options, "Debug.circle");
}

/*
 * Many shapes in one call: a Float32Array of 4 floats per shape, checked and
 * queued in C. For hitboxes of many entities, far cheaper than one call per
 * shape. Groups with a value that is not finite are skipped.
 */
function queueBatch(kind, values, color, options, name) {
    if (!(values instanceof Float32Array)) throw new TypeError(`${name} expects a Float32Array`);
    if (values.length % 4) throw new RangeError(`${name}: the array length must be a multiple of 4`);
    shapeOptions(options, name);
    const queued = Native.shapes(kind, values, color, shapeSeconds, shapeFlags);
    state.shapeCount += queued;
    if (queued) useSystem();
    return queued;
}

/** Rectangles from x, y, width, height groups; returns how many were queued. */
export function rects(values, color, options) {
    return queueBatch(RECT, values, checkColor(color, SHAPE, "Debug.rects"), options, "Debug.rects");
}

/** Lines from x1, y1, x2, y2 groups; returns how many were queued. */
export function lines(values, color, options) {
    return queueBatch(LINE, values, checkColor(color, SHAPE, "Debug.lines"), options, "Debug.lines");
}

/** Text at (x, y); `options.color` sets its color. */
export function text(x, y, value, options) {
    checkNumber(x, "Debug.text x");
    checkNumber(y, "Debug.text y");
    const color = checkColor(options && typeof options === "object" ? options.color : undefined,
        TEXT, "Debug.text");
    shapeOptions(options, "Debug.text");
    const texts = state.texts;
    texts.push({ x, y, value: String(value), color, world: (shapeFlags & WORLD) !== 0,
        remaining: shapeSeconds });
    // Bounded like the native queue; trimmed in batches.
    if (texts.length >= 2 * MAX_TEXTS) {
        const excess = texts.length - MAX_TEXTS;
        texts.splice(0, excess);
        state.textsDropped += excess;
    }
    useSystem();
}

/** Removes every shape and text still on screen. */
export function clear() {
    Native.shapesClear();
    state.shapeCount = 0;
    state.texts.length = 0;
    state.textsDropped = 0;
    releaseSystem();
}

/**
 * Shows and hides everything when the `buttons` combination (e.g.
 * Gamepad.L3 | Gamepad.R3) is pressed on the controller of `port`. Reads the
 * pad without touching Gamepad.update(), so the game's justPressed() is
 * unaffected. `null` removes the shortcut.
 */
export function toggleWith(buttons, port = 0) {
    if (buttons === null) {
        state.toggle = null;
        releaseSystem();
        return;
    }
    checkNumber(buttons, "Debug.toggleWith buttons");
    if (buttons <= 0 || buttons !== Math.floor(buttons) || buttons > 0xFFFF)
        throw new RangeError("Debug.toggleWith buttons must be Gamepad button bits");
    if (port !== 0 && port !== 1) throw new RangeError("Debug.toggleWith port must be 0 or 1");
    state.toggle = { buttons, port, held: true };   // no toggle until released once
    useSystem();
}

/** Shows or hides everything (what the shortcut does); returns whether shown. */
export function show(on) {
    if (on !== undefined) state.shown = !!on;
    return state.shown;
}

/**
 * World space for shapes drawn with `space: "world"`: screen = (world - {x, y})
 * * scale. A camera can call this every frame.
 */
export function setView(view) {
    if (view === null || typeof view !== "object") throw new TypeError("Debug.setView expects an object");
    state.view = {
        x: view.x === undefined ? 0 : checkNumber(view.x, "Debug.setView x"),
        y: view.y === undefined ? 0 : checkNumber(view.y, "Debug.setView y"),
        scale: view.scale === undefined ? 1 : checkNumber(view.scale, "Debug.setView scale"),
    };
}

/**
 * Options:
 *   budgetMs       frame budget of the graph colors; 0 (default) derives it
 *                  from the video mode (60 or 50 Hz) and vsyncInterval
 *   vsyncInterval  the one given to Loop.run() (default 1; 2 for 30 fps)
 *   margin         distance from the screen edges, a number or { x, y };
 *                  null (default) is the title-safe area (5% of each side)
 *   font           font of every text (default: the built-in one at 16 px)
 */
export function configure(options) {
    if (options === null || typeof options !== "object") throw new TypeError("Debug.configure expects an object");
    if (options.budgetMs !== undefined) {
        const value = checkNumber(options.budgetMs, "Debug.configure budgetMs");
        if (value < 0) throw new RangeError("Debug.configure budgetMs must be 0 (automatic) or positive");
        state.budgetMs = value;
    }
    if (options.vsyncInterval !== undefined) {
        const value = checkNumber(options.vsyncInterval, "Debug.configure vsyncInterval");
        if (value < 1 || value > 4 || value !== Math.floor(value))
            throw new RangeError("Debug.configure vsyncInterval must be an integer from 1 to 4");
        state.vsyncInterval = value;
    }
    if (options.margin !== undefined) {
        const margin = options.margin;
        if (margin === null) {
            state.margin = null;
        } else if (typeof margin === "number") {
            checkNumber(margin, "Debug.configure margin");
            state.margin = { x: margin, y: margin };
        } else if (typeof margin === "object") {
            state.margin = { x: checkNumber(margin.x, "Debug.configure margin.x"),
                y: checkNumber(margin.y, "Debug.configure margin.y") };
        } else {
            throw new TypeError("Debug.configure margin must be a number, { x, y } or null");
        }
    }
    if (options.font !== undefined) {
        if (!(options.font instanceof Font)) throw new TypeError("Debug.configure font must be a Font");
        state.font = options.font;
        invalidateText();
    }
    state.refreshIn = 0;
}

/**
 * For games that do not use Loop.run(): call after drawing, before
 * Screen.flip(). `dt` in seconds ages the shapes and gives the FPS; `cpuMs`
 * feeds the graph. With Loop.run() the module already draws itself.
 */
export function frame(dt, cpuMs = 0) {
    checkNumber(dt, "Debug.frame dt");
    checkNumber(cpuMs, "Debug.frame cpuMs");
    if (state.system && Loop.isRunning()) {
        if (!state.warnedFrame)
            log("Debug.frame(): Loop.run() already draws the debug overlay; this frame is skipped");
        state.warnedFrame = true;
        return;
    }
    age(dt);
    render({ fps: dt > 0 ? 1 / dt : 0, frameMs: dt * 1000, cpuMs, steps: 1 });
}

/** Figures of the frame-time graph over the last `frames` frames (default 60). */
export function frameStats(frames = STATS_FRAMES) {
    return Native.stats(checkNumber(frames, "Debug.frameStats frames"));
}
