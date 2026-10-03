/*
 * Scenes and their assets.
 *
 * Scene is the base class of a game's screens (title, level, pause menu):
 * a manifest of the assets it needs and the enter/update/draw/pause/resume/
 * exit lifecycle. The manager (static methods of Scene) keeps a stack of
 * scenes, loads the next one's assets in the background while the current
 * one fades out, shows a loading screen only when loading outlasts the
 * transition, and releases what the previous scene held.
 *
 * Assets are reference counted by kind and path: an asset two scenes use is
 * loaded once, and survives going from one to the other, because the next
 * scene's assets are acquired before the previous scene's are released.
 *
 * Optional modules (ImageList, Sound, Font, Sprite, Camera2D) are looked up
 * when used, so a build without them still runs scenes that do not need them.
 * Transitions and loading run on real time: a pause menu that sets
 * Loop.setTimeScale(0) can still go to another scene.
 */
import * as Loop from "Loop";
import * as Draw from "Draw";
import * as Screen from "Screen";
import * as Color from "Color";

const g = globalThis;

function optional(name, what) {
    const module = g[name];
    if (module === undefined)
        throw new Error(`${what} needs the ${name} module, which is not in this build`);
    return module;
}

function joinPath(root, path) {
    if (!root || path.includes(":") || path.startsWith("/")) return path;
    return (root.endsWith("/") ? root : root + "/") + path;
}

function isThenable(value) {
    return value !== null && (typeof value === "object" || typeof value === "function") &&
        typeof value.then === "function";
}

/* Runs `fn` in screen space: over the camera, when Camera2D is in the build. */
function screenSpace(fn) {
    if (g.Camera2D && typeof g.Camera2D.screenSpace === "function") g.Camera2D.screenSpace(fn);
    else fn();
}

/* Calls every function, and throws the first error once all ran. */
function runAll(fns) {
    let first = null;
    for (const fn of fns) {
        try { fn(); } catch (error) { if (first === null) first = error; }
    }
    if (first !== null) throw first;
}

/* ---------------------------------------------------------------------------------------------- */
/* Assets                                                                                         */

const entries = new Map();      /* "kind|key" -> Entry */
const owners = new WeakMap();   /* asset object -> Entry, for release(asset) */
const loaders = new Map();      /* kind -> { load, free, key } */
const pendingImages = new Set();
/* Frees asked for while a frame is being built, run at the start of the next one. */
const deferredFrees = [];
let imageList = null;
/* Seconds the ImageList has had nothing to do: its decoder thread closes after imageListIdleTime. */
let imageListIdle = 0;
let assetSystem = null;
/* True inside the module's own preUpdate: nothing of this frame was drawn yet. */
let safeToFree = false;
/*
 * Loads with a timeout: { left (seconds), expire() }, counted down by the
 * real time of each frame. Not Date.now(): numbers are float32 in this
 * QuickJS, and milliseconds since 1970 only keep steps of about two minutes.
 */
const watchers = new Set();

/* An error for a load that took too long, listing what was still loading. */
function timeoutError(message, pending) {
    const list = pending.map(item => `${item.kind} '${item.path}'`).join(", ");
    const error = new Error(`${message}${list ? `; still loading: ${list}` : ""}`);
    error.name = "TimeoutError";
    error.code = "TIMEOUT";
    error.pending = pending;
    return error;
}

function watch(seconds, expire) {
    const watcher = { left: seconds, expire };
    watchers.add(watcher);
    useAssetSystem();
    return watcher;
}

function checkWatchers(dt) {
    if (watchers.size === 0) return;
    for (const watcher of [...watchers]) {
        watcher.left -= dt > 0 ? dt : 0;
        if (watcher.left > 0) continue;
        watchers.delete(watcher);
        watcher.expire();
    }
}

/* A timeout option: seconds, finite and above zero, or undefined. */
function readTimeout(value, where) {
    if (value === undefined) return undefined;
    if (typeof value !== "number" || !(value > 0) || value === Infinity)
        throw new RangeError(`${where}: timeout must be a finite number of seconds above zero`);
    return value;
}

/* The options argument of load()/acquire(): a root path, or { root, timeout }. */
function readLoadOptions(value, where, defaultRoot = "") {
    if (value === undefined || typeof value === "string") return { root: value ?? defaultRoot };
    if (value === null || typeof value !== "object" || Array.isArray(value))
        throw new TypeError(`${where}: options must be a root path or { root, timeout }`);
    if (value.root !== undefined && typeof value.root !== "string")
        throw new TypeError(`${where}: root must be a string`);
    return { root: value.root ?? defaultRoot, timeout: readTimeout(value.timeout, where) };
}

class Entry {
    constructor(kind, key, path, spec, loader) {
        this.kind = kind;
        this.key = key;
        this.path = path;
        this.spec = spec;
        this.loader = loader;
        this.refs = 1;
        this.state = "loading";
        this.value = undefined;
        this.error = null;
        this.cancel = null;
        this.promise = null;
    }
}

/*
 * Frees an asset now when nothing of the current frame can use it (the
 * start of a frame, or no Loop), else at the start of the next frame: an
 * Image freed during draw() would leave the frame's GS packet pointing at
 * VRAM and pixels that are gone.
 */
function freeAsset(entry, value) {
    if (safeToFree || !Loop.isRunning()) {
        entry.loader.free(value, entry.spec);
        return;
    }
    deferredFrees.push({ loader: entry.loader, value, spec: entry.spec });
    useAssetSystem();
}

function flushFrees() {
    if (deferredFrees.length === 0) return;
    runAll(deferredFrees.splice(0).map(item => () => item.loader.free(item.value, item.spec)));
}

function assetWorkLeft() {
    return pendingImages.size > 0 || deferredFrees.length > 0 || imageList !== null ||
        watchers.size > 0;
}

/*
 * Pumps the ImageList, runs deferred frees and closes an idle ImageList: a
 * Loop system on real time, while there is any of that to do.
 */
function useAssetSystem() {
    if (assetSystem || !assetWorkLeft()) return;
    assetSystem = Loop.addSystem({
        name: "scene.assets",
        priority: -200,
        realTime: true,
        preUpdate(realDt) { pumpAssets(realDt); },
    });
}

function pumpAssets(dt) {
    safeToFree = true;
    try {
        flushFrees();
        checkWatchers(dt);
        if (imageList && pendingImages.size > 0) {
            imageListIdle = 0;
            imageList.process({ maxItems: 64, maxTime: Assets.budgetMs });
        } else if (imageList) {
            /* Nothing to decode: after a while the decoder thread and its stack go. */
            imageListIdle += dt > 0 ? dt : 0;
            if (imageListIdle >= Assets.imageListIdleTime) {
                imageList.close();
                imageList = null;
                imageListIdle = 0;
            }
        }
    } finally {
        safeToFree = false;
        if (assetSystem && !assetWorkLeft()) {
            Loop.removeSystem(assetSystem);
            assetSystem = null;
        }
    }
}

function sharedImageList() {
    if (!imageList) {
        const ImageList = optional("ImageList", "Scene.Assets images");
        imageList = new ImageList({ workers: 1 });
        imageListIdle = 0;
    }
    return imageList;
}

/* What an images spec asks of the texture: "draw" (undefined), "bind" or "lock". */
function uploadOf(spec) {
    return spec.upload ?? (spec.lock ? "lock" : undefined);
}

/* Reads a file: on the job pool with Thread.readFileAsync, else at once with std.loadFile. */
function readFile(path, text, what) {
    const Thread = g.Thread;
    if (Thread && typeof Thread.readFileAsync === "function")
        return Thread.readFileAsync(path, { text });
    if (!text)
        throw new Error(`${what} needs the Thread module, which is not in this build`);
    const data = optional("std", what).loadFile(path);
    if (data === null) throw Object.assign(new Error(`cannot read '${path}'`), { path });
    return data;
}

function parseJson(text, path) {
    try {
        return JSON.parse(text);
    } catch (error) {
        throw Object.assign(new SyntaxError(`invalid JSON in '${path}': ${error.message}`), { path });
    }
}

/* Unlocks and frees the VRAM and pixels now, instead of when the collector finds the Image. */
function freeImage(image) {
    if (typeof image.locked === "function" && image.locked()) image.unlock();
    if (typeof image.free === "function") image.free();
}

/* A cache key for options that may hold objects (an Image): each object by identity. */
const objectIds = new WeakMap();
let nextObjectId = 1;
function optionsKey(options) {
    return JSON.stringify(options, (key, value) => {
        if (value === null || typeof value !== "object" || Array.isArray(value) ||
            Object.getPrototypeOf(value) === Object.prototype)
            return value;
        if (!objectIds.has(value)) objectIds.set(value, nextObjectId++);
        return `#object${objectIds.get(value)}`;
    });
}

/* The loaders of the built-in kinds. load(path, spec, handle) returns the asset or a promise. */
const builtins = {
    /* { path, upload: "draw" | "bind" | "lock", lock, priority } */
    images: {
        load(path, spec, handle) {
            const upload = uploadOf(spec);
            if (g.ImageList === undefined) {
                /* No ImageList: a synchronous load, as new Image() does. */
                const image = new (optional("Image", "Scene.Assets images"))(path);
                if (upload === "lock") image.lock();
                return image;
            }
            return new Promise((resolve, reject) => {
                const image = sharedImageList().load(path, {
                    upload,
                    priority: spec.priority,
                    onLoad(loaded) {
                        pendingImages.delete(loaded);
                        resolve(loaded);
                    },
                    onError(failed, error) {
                        pendingImages.delete(failed);
                        reject(Object.assign(new Error(`cannot load image '${path}'` +
                            (error && error.stage ? ` (${error.stage})` : "")), { path, cause: error }));
                    },
                });
                pendingImages.add(image);
                handle.cancel = () => {
                    pendingImages.delete(image);
                    imageList.cancel(image);
                };
                useAssetSystem();
            });
        },
        free: freeImage,
        /* A cached image asked for again: a "lock" asks for more than the first load did. */
        reuse(image, spec) {
            if (uploadOf(spec) === "lock" && typeof image.locked === "function" && !image.locked())
                image.lock();
        },
    },
    /* "hero.json" (Aseprite, TexturePacker) or { path: "hero.png", frameWidth, frameHeight, ... } */
    sheets: {
        load(path, spec) {
            const Sprite = optional("Sprite", "Scene.Assets sheets");
            const { path: _path, ...options } = spec;
            return path.toLowerCase().endsWith(".json") ?
                Sprite.Sheet.fromJSONAsync(path, options) : Sprite.Sheet.fromGridAsync(path, options);
        },
        /* The texture it loaded; an options.image is the game's, not ours to free. */
        free(sheet, spec) {
            if (sheet.image && !(spec && spec.image)) freeImage(sheet.image);
        },
        key(path, spec) {
            const { path: _path, ...options } = spec;
            return path + optionsKey(options);
        },
    },
    /* ADPCM sound effects, uploaded to SPU2 memory. */
    sfx: {
        load(path) { return optional("Sound", "Scene.Assets sfx").loadSfxAsync(path); },
        free(sfx) { sfx.free(); },
    },
    /* Streams (WAV, Ogg): opened now, read while playing. { path, loop } */
    music: {
        load(path, spec) {
            const stream = new (optional("Sound", "Scene.Assets music").Stream)(path);
            if (spec.loop !== undefined) stream.loop = !!spec.loop;
            return stream;
        },
        free(stream) { stream.free(); },
    },
    /* { path, size, preload } (preload: true or the characters to rasterize ahead). */
    fonts: {
        load(path, spec) {
            const Font = optional("Font", "Scene.Assets fonts");
            const options = {};
            if (spec.size !== undefined) options.size = spec.size;
            if (spec.preload !== undefined) options.preload = spec.preload;
            return Font.loadAsync(path, options);
        },
        free(font) { font.free(); },
        key(path, spec) { return `${path}@${spec.size ?? ""}`; },
    },
    /*
     * JSON files, read on the job pool and parsed on the script thread
     * (JavaScript objects can only be built there: keep files small, or
     * split a large level into several).
     */
    data: {
        load(path) {
            const read = readFile(path, true, "Scene.Assets data");
            return isThenable(read) ? Promise.resolve(read).then(text => parseJson(text, path)) :
                parseJson(read, path);
        },
        free() {},
    },
    /* Text files (UTF-8), as strings. */
    text: {
        load(path) { return readFile(path, true, "Scene.Assets text"); },
        free() {},
    },
    /* Any file, as an ArrayBuffer (needs the Thread module). */
    binary: {
        load(path) { return readFile(path, false, "Scene.Assets binary"); },
        free() {},
    },
};
for (const kind of Object.keys(builtins)) loaders.set(kind, builtins[kind]);

function normalizeSpec(kind, spec, where) {
    if (typeof spec === "string") return { path: spec };
    if (spec === null || typeof spec !== "object" || Array.isArray(spec))
        throw new TypeError(`${where}: a ${kind} asset must be a path or { path, ... }`);
    if (typeof spec.path !== "string")
        throw new TypeError(`${where}: a ${kind} asset needs a path`);
    return spec;
}

/*
 * One more holder of an asset: the cached entry (also one still loading
 * that nobody holds, which is reused instead of loaded twice), or a new one
 * that starts loading.
 */
function acquireEntry(kind, spec, root, where) {
    const loader = loaders.get(kind);
    if (!loader)
        throw new TypeError(`${where}: unknown asset kind '${kind}' (Scene.Assets.define() adds kinds)`);
    spec = normalizeSpec(kind, spec, where);
    const path = joinPath(root, spec.path);
    const key = kind + "|" + (loader.key ? loader.key(path, spec) : path);
    const cached = entries.get(key);
    if (cached) {
        cached.refs++;
        if (loader.reuse) {
            if (cached.state === "ready") loader.reuse(cached.value, spec);
            else (cached.reuses ??= []).push(spec);
        }
        return cached;
    }
    const entry = new Entry(kind, key, path, spec, loader);
    entries.set(key, entry);
    entry.promise = new Promise((resolve, reject) => {
        let result;
        try {
            result = loader.load(path, spec, entry);
        } catch (error) {
            reject(error);
            return;
        }
        if (isThenable(result)) result.then(resolve, reject);
        else resolve(result);
    }).then(value => {
        if (entry.refs === 0) {
            /* Released while loading and not acquired again: nobody wants it. */
            if (entries.get(key) === entry) entries.delete(key);
            entry.state = "released";
            freeAsset(entry, value);
            return value;
        }
        entry.state = "ready";
        entry.value = value;
        if (value !== null && typeof value === "object") owners.set(value, entry);
        /* What later holders asked for while it loaded (a lock). */
        for (const later of entry.reuses ?? []) loader.reuse(value, later);
        entry.reuses = null;
        return value;
    }, error => {
        entry.state = "failed";
        entry.error = error instanceof Error ? error :
            Object.assign(new Error(String(error)), { path });
        if (entries.get(key) === entry) entries.delete(key);   /* a later acquire retries */
        throw entry.error;
    });
    /* Holders observe failures through their own chains. */
    entry.promise.catch(() => {});
    return entry;
}

function releaseEntry(entry) {
    if (entry.refs <= 0) return;
    if (--entry.refs > 0) return;
    if (entry.state === "ready") {
        if (entries.get(entry.key) === entry) entries.delete(entry.key);
        const value = entry.value;
        if (value !== null && typeof value === "object") owners.delete(value);
        entry.value = undefined;
        entry.state = "released";
        freeAsset(entry, value);
    } else if (entry.state === "loading" && entry.cancel) {
        /* A queued image: cancelled, as a later acquire would load it again anyway. */
        if (entries.get(entry.key) === entry) entries.delete(entry.key);
        entry.state = "cancelled";
        entry.cancel();
    }
    /* Other loads keep running, cached with no holder: freed when they end, or reused. */
}

/*
 * The assets of a manifest ({ kind: { name: spec } }), acquired together:
 * `assets` mirrors the manifest with the loaded values, `progress` goes from
 * 0 to 1, `ready` resolves with `assets` (or rejects with the first error).
 */
class AssetGroup {
    constructor(manifest, root, where, timeout) {
        if (manifest === null || typeof manifest !== "object" || Array.isArray(manifest))
            throw new TypeError(`${where}: the manifest must be an object { kind: { name: spec } }`);
        this.assets = {};
        this.error = null;
        this.released = false;
        this._entries = [];
        this._loaded = 0;
        try {
            for (const kind of Object.keys(manifest)) {
                const items = manifest[kind];
                if (items === null || typeof items !== "object" || Array.isArray(items))
                    throw new TypeError(`${where}: manifest.${kind} must be an object { name: spec }`);
                const out = this.assets[kind] = {};
                for (const name of Object.keys(items))
                    this._entries.push({ out, name, entry: acquireEntry(kind, items[name], root, where) });
            }
        } catch (error) {
            this.release();
            throw error;
        }
        this.total = this._entries.length;
        /* Cached assets count at once: a scene whose assets are all cached needs no frame. */
        for (const item of this._entries) {
            if (item.entry.state === "ready") this._count(item, item.entry.value);
        }
        this.ready = new Promise((resolve, reject) => {
            this._reject = reject;
            Promise.all(this._entries.map(item =>
                item.entry.promise.then(value => this._count(item, value))))
                .then(() => {
                    this._unwatch();
                    resolve(this.assets);
                }, error => this._fail(error));
        });
        this.ready.catch(() => {});
        if (timeout !== undefined && !this.done) {
            this._watcher = watch(timeout, () => {
                this._watcher = null;
                if (!this.done && !this.error)
                    this._fail(timeoutError(`${where}: not loaded after ${timeout} s`, this.pending()));
            });
        }
    }

    _fail(error) {
        if (this.error) return;
        this.error = error;
        this._unwatch();
        this._reject(error);
    }

    _unwatch() {
        if (this._watcher) {
            watchers.delete(this._watcher);
            this._watcher = null;
        }
    }

    /* What is still loading: [{ kind, name, path, state }]. */
    pending() {
        return this._entries.filter(item => !item.counted).map(item => ({
            kind: item.entry.kind, name: item.name, path: item.entry.path, state: item.entry.state,
        }));
    }

    _count(item, value) {
        if (item.counted) return;
        item.counted = true;
        item.out[item.name] = value;
        this._loaded++;
    }

    get loaded() { return this._loaded; }
    get progress() { return this.total === 0 ? 1 : this.loaded / this.total; }
    get done() { return this.loaded >= this.total && !this.error; }
    get failed() { return this.error !== null; }

    /* Lets go of every asset of the group; each is freed once nobody else holds it. */
    release() {
        if (this.released) return;
        this.released = true;
        this._unwatch();
        for (const item of this._entries) releaseEntry(item.entry);
    }
}

export const Assets = {
    /* Milliseconds of ImageList work per frame while images load. */
    budgetMs: 4,
    /* Seconds without images to load before the ImageList's decoder thread closes. */
    imageListIdleTime: 5,

    /* A group of assets; the second argument is a root path or { root, timeout } (seconds). */
    acquire(manifest, options) {
        const { root, timeout } = readLoadOptions(options, "Scene.Assets.acquire");
        return new AssetGroup(manifest, root, "Scene.Assets.acquire", timeout);
    },

    /*
     * One asset: a promise of it. Release it with release(asset). The third
     * argument is a root path or { root, timeout }: past the timeout the
     * promise rejects (the load goes on, and is freed if nobody holds it).
     */
    load(kind, spec, options) {
        const { root, timeout } = readLoadOptions(options, "Scene.Assets.load");
        const entry = acquireEntry(kind, spec, root, "Scene.Assets.load");
        if (timeout === undefined || entry.state === "ready") return entry.promise;
        return new Promise((resolve, reject) => {
            const watcher = watch(timeout, () => {
                reject(timeoutError(`Scene.Assets.load: not loaded after ${timeout} s`,
                    [{ kind, name: entry.path, path: entry.path, state: entry.state }]));
                /* Its hold goes: the load finishes and is freed unless someone else wants it. */
                releaseEntry(entry);
            });
            entry.promise.then(value => {
                if (!watchers.has(watcher)) return;
                watchers.delete(watcher);
                resolve(value);
            }, error => {
                watchers.delete(watcher);
                reject(error);
            });
        });
    },

    /*
     * Releases one hold of an asset from load(): by the asset object, or
     * by the kind and spec it was loaded with (needed for strings, which
     * text files are). False when it is not held.
     */
    release(asset, spec, root = "") {
        let entry;
        if (spec === undefined) {
            entry = owners.get(asset);
        } else {
            const loader = loaders.get(asset);
            if (!loader)
                throw new TypeError(`Scene.Assets.release: unknown asset kind '${asset}'`);
            const normal = normalizeSpec(asset, spec, "Scene.Assets.release");
            const path = joinPath(root, normal.path);
            entry = entries.get(asset + "|" + (loader.key ? loader.key(path, normal) : path));
        }
        if (!entry || entry.refs <= 0) return false;
        releaseEntry(entry);
        return true;
    },

    /*
     * A new kind of asset: load(path, spec) returns the asset or a promise
     * of it; free(asset, spec) releases it; key(path, spec), optional, tells
     * apart loads of one path with different options; reuse(asset, spec),
     * optional, applies what a later holder of a cached asset asks for.
     */
    define(kind, loader) {
        if (typeof kind !== "string" || kind === "")
            throw new TypeError("Scene.Assets.define: the kind must be a name");
        if (loader === null || typeof loader !== "object" || typeof loader.load !== "function" ||
            typeof loader.free !== "function" ||
            (loader.key !== undefined && typeof loader.key !== "function") ||
            (loader.reuse !== undefined && typeof loader.reuse !== "function"))
            throw new TypeError("Scene.Assets.define: the loader must be { load(path, spec), free(asset), key?, reuse? }");
        loaders.set(kind, loader);
    },

    /* How many assets are held, loading, and by kind: for Debug.watch(). */
    stats() {
        const byKind = {};
        let loading = 0, refs = 0;
        for (const entry of entries.values()) {
            byKind[entry.kind] = (byKind[entry.kind] ?? 0) + 1;
            if (entry.state === "loading") loading++;
            refs += entry.refs;
        }
        return { entries: entries.size, loading, refs, byKind, pendingFrees: deferredFrees.length,
            imageListOpen: imageList !== null };
    },

    /* Every held asset, for finding leaks: [{ kind, path, refs, state }]. */
    list() {
        return [...entries.values()].map(e => ({ kind: e.kind, path: e.path, refs: e.refs, state: e.state }));
    },

    /*
     * The ImageList work, deferred frees and idle close of a frame, `dt`
     * seconds long: for games without Loop.run().
     */
    update(dt = 0) { pumpAssets(dt); },
};

/* ---------------------------------------------------------------------------------------------- */
/* Scenes                                                                                         */

/* Stack entries: { scene, group, drawBelow, updateBelow }. The top is the current scene. */
const stack = [];
const queue = [];
/* Scene.preload(): groups held until their scene class enters. */
const preloads = new Map();
let task = null;
let managerSystem = null;
let pendingError = null;

/* A scene's own clean-ups and asset groups (defer(), acquire()), kept off its properties. */
const CLEANUPS = Symbol("Scene cleanups");
const GROUPS = Symbol("Scene groups");

const settings = {
    loadingScreen: null,
    /* Seconds of loading after which a scene's pending assets are logged once (0: never). */
    slowLoadWarning: 10,
    /* Seconds after which a scene that has not loaded fails (0: never). */
    loadTimeout: 0,
    /* Loading shorter than this shows no loading screen (no flash). */
    loadingDelay: 0.15,
    minLoadingTime: 0.3,
    onError: null,
};

function defaultLoadingScreen(progress) {
    const { width, height } = Screen.getMode();
    const w = Math.round(width / 2), h = 8;
    const x = Math.round((width - w) / 2), y = Math.round(height * 0.75);
    Draw.rect(x - 2, y - 2, w + 4, h + 4, Color.new(90, 90, 90));
    Draw.rect(x, y, w, h, Color.new(20, 20, 20));
    const filled = Math.round(w * Math.min(1, Math.max(0, progress)));
    if (filled >= 1) Draw.rect(x, y, filled, h, Color.new(230, 230, 230));
}
settings.loadingScreen = defaultLoadingScreen;

function smooth(t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* Wipe directions: the way the edge moves across the screen. */
const WIPES = ["left", "right", "up", "down"];

/*
 * Draws a transition covering `amount` (0..1) of the screen, in screen
 * space: fade blends the color over it, wipe slides a band of it across.
 */
const transitions = new Map([
    ["fade", (amount, info) => {
        const c = info.color >>> 0;
        Draw.rect(0, 0, info.width, info.height, Color.new(c & 255, (c >>> 8) & 255,
            (c >>> 16) & 255, Math.round(128 * amount)));
    }],
    ["wipe", (amount, info) => {
        const { width: w, height: h } = info;
        const c = info.color >>> 0;
        const color = Color.new(c & 255, (c >>> 8) & 255, (c >>> 16) & 255);
        const horizontal = info.direction === "left" || info.direction === "right";
        const size = Math.round((horizontal ? w : h) * amount);
        if (size < 1) return;
        /* Out, the band grows from the edge the wipe starts at; in, it shrinks ahead of it. */
        const fromEnd = (info.direction === "left" || info.direction === "up") === (info.phase !== "in");
        if (horizontal) Draw.rect(fromEnd ? w - size : 0, 0, size, h, color);
        else Draw.rect(0, fromEnd ? h - size : 0, w, size, color);
    }],
]);

function isCustomTransition(value) {
    return value !== null && typeof value === "object" && !Array.isArray(value) &&
        typeof value.draw === "function";
}

function readOptions(options, where) {
    if (options === undefined) options = {};
    if (options === null || typeof options !== "object" || Array.isArray(options))
        throw new TypeError(`${where} options must be an object`);
    const transition = options.transition ?? "none";
    if (transition !== "none" && !transitions.has(transition) && !isCustomTransition(transition))
        throw new TypeError(`${where}: transition must be "none", "fade", "wipe", a name from ` +
            "Scene.defineTransition() or { draw(amount, info) }");
    if (options.direction !== undefined && !WIPES.includes(options.direction))
        throw new TypeError(`${where}: direction must be "left", "right", "up" or "down"`);
    const duration = options.duration ?? 0.4;
    if (typeof duration !== "number" || !(duration >= 0) || duration === Infinity)
        throw new RangeError(`${where}: duration must be a finite number of seconds, zero or more`);
    if (options.color !== undefined && typeof options.color !== "number")
        throw new TypeError(`${where}: color must be a color (Color.new())`);
    const timeout = readTimeout(options.timeout, where);
    /* The defaults filled in: a transition without a duration uses 0.4 s. */
    return { ...options, transition, duration, timeout };
}

function checkSceneClass(SceneClass, where) {
    if (typeof SceneClass !== "function" || !(SceneClass === Scene || SceneClass.prototype instanceof Scene))
        throw new TypeError(`${where} expects a class that extends Scene`);
}

/* The manifest of a scene class: static assets, an object or a function of the params. */
function manifestOf(SceneClass, params) {
    const assets = SceneClass.assets;
    if (typeof assets === "function") return assets.call(SceneClass, params) ?? {};
    return assets ?? {};
}

/* Real time for transitions and loading: a Loop system, added on the first request. */
function useManagerSystem() {
    if (managerSystem) return;
    managerSystem = Loop.addSystem({
        name: "scene",
        priority: -150,
        realTime: true,
        preUpdate(realDt) {
            safeToFree = true;
            try {
                tick(realDt);
            } finally {
                safeToFree = false;
            }
        },
    });
}

function request(kind, SceneClass, options, where) {
    options = readOptions(options, where);
    useManagerSystem();
    const promise = new Promise((resolve, reject) => {
        queue.push({ kind, SceneClass, options, where, resolve, reject });
        if (!task) startNext();
    });
    /* Failures are reported (onError or the next update); awaiting is optional. */
    promise.catch(() => {});
    return promise;
}

/* A failure with the request and scene it belongs to: "Scene.go(Level1): cannot load ...". */
function describe(where, SceneClass, error) {
    const Ctor = error instanceof Error ? error.constructor : Error;
    const name = SceneClass ? `(${SceneClass.name})` : "";
    const message = error instanceof Error ? error.message : String(error);
    const described = new Ctor(`${where}${name}: ${message}`);
    described.cause = error;
    described.scene = SceneClass;
    /* What the original carries: the file, and a timeout's name, code and pending list. */
    for (const key of ["path", "code", "pending"])
        if (error && error[key] !== undefined) described[key] = error[key];
    if (error && error.name === "TimeoutError") described.name = error.name;
    return described;
}

/* A request that failed: Scene.onError, or thrown from the next Scene.update(). */
function report(error, SceneClass) {
    if (settings.onError) settings.onError(error, SceneClass);
    else if (!pendingError) pendingError = error;
}

function startNext() {
    task = null;
    while (!task && queue.length > 0) {
        const next = queue.shift();
        const fade = (next.options.transition ?? "none") !== "none" && stack.length > 0;
        const half = fade ? next.options.duration / 2 : 0;
        task = {
            ...next,
            phase: "out",
            time: 0,
            outTime: half,
            inTime: fade ? half : 0,
            shown: 0,
            loadTime: 0,
            warned: false,
            group: null,
            scene: null,
            params: next.options.params,
            color: next.options.color ?? Color.new(0, 0, 0),
        };
        if (task.kind === "pop" && stack.length === 0) {
            const error = new Error(`${task.where}: there is no scene to pop`);
            task.reject(error);
            report(error, null);
            task = null;
            continue;
        }
        try {
            if (task.kind !== "pop") {
                task.scene = new task.SceneClass(task.params);
                task.manifest = manifestOf(task.SceneClass, task.params);
                /* Loads during the fade-out, before the old assets go. */
                if (!(task.kind === "go" && next.options.unloadFirst))
                    task.group = new AssetGroup(task.manifest, task.SceneClass.root ?? "", task.where);
            }
        } catch (error) {
            const described = describe(task.where, task.SceneClass, error);
            task.reject(described);
            report(described, task.SceneClass);
            task = null;
        }
    }
}

/*
 * A scene leaves: exit() (unless its enter() failed), then its defer()
 * clean-ups, its acquire() groups and its manifest's assets. Everything
 * runs even when one of them throws; the first error is thrown at the end.
 */
function finishScene(entry, callExit) {
    const scene = entry.scene;
    const steps = [];
    if (callExit) steps.push(() => scene.exit());
    const cleanups = scene[CLEANUPS] ?? [];
    while (cleanups.length > 0) {
        const fn = cleanups.pop();
        steps.push(() => fn.call(scene));
    }
    for (const group of (scene[GROUPS] ?? []).splice(0)) steps.push(() => group.release());
    steps.push(() => entry.group.release());
    steps.push(() => { scene.assets = null; });
    runAll(steps);
}

/* Exits and releases every scene of the stack, top first, even when one throws. */
function clearStack() {
    const steps = [];
    while (stack.length > 0) {
        const entry = stack.pop();
        steps.push(() => finishScene(entry, true));
    }
    runAll(steps);
}

function collectGarbage() {
    if (g.std && typeof g.std.gc === "function") g.std.gc();
}

function releasePreloads(SceneClass) {
    const groups = preloads.get(SceneClass);
    if (!groups) return;
    preloads.delete(SceneClass);
    for (const group of groups) group.release();
}

/* The switch itself, between the fade-out and the fade-in. */
function apply(t) {
    const top = stack[stack.length - 1];
    if (t.kind === "pop") {
        const entry = stack.pop();
        try {
            finishScene(entry, true);
        } finally {
            collectGarbage();
        }
        const below = stack[stack.length - 1];
        if (below) below.scene.resume(t.options.result);
        return undefined;
    }
    const replaced = t.kind === "replace" ? top : null;
    const entry = {
        scene: t.scene,
        group: t.group,
        drawBelow: t.kind === "push" || replaced ?
            (t.options.drawBelow ?? (replaced ? replaced.drawBelow : true)) : false,
        updateBelow: t.kind === "push" || replaced ?
            (t.options.updateBelow ?? (replaced ? replaced.updateBelow : false)) : false,
    };
    if (t.kind === "go") {
        if (!t.options.unloadFirst) {
            try {
                clearStack();
            } finally {
                collectGarbage();
            }
        }
    } else if (replaced) {
        stack.pop();
        try {
            finishScene(replaced, true);
        } finally {
            collectGarbage();
        }
    } else if (top) {
        top.scene.pause();
    }
    stack.push(entry);
    t.scene.assets = t.group.assets;
    /* The preloaded assets are the scene's own now: the preload's holds can go. */
    releasePreloads(t.SceneClass);
    return t.scene.enter(t.group.assets, t.params);
}

/*
 * A switch that threw (an exit(), pause() or enter() of the game): the
 * request ends and the manager goes on with the next one. A scene whose
 * enter() failed leaves the stack without exit().
 */
function abort(t, error) {
    if (task === t) task = null;
    t.phase = "failed";
    const top = stack[stack.length - 1];
    try {
        if (t.scene && top && top.scene === t.scene) {
            stack.pop();
            finishScene(top, false);
        } else if (t.group && !stack.some(entry => entry.group === t.group)) {
            t.group.release();
        }
    } catch (cleanupError) {
        /* The first error is the one reported. */
    }
    t.reject(error);
    startNext();
}

/* An asset of the next scene failed: it does not enter, the current scene stays. */
function fail(t, error) {
    const described = describe(t.where, t.SceneClass, error);
    if (t.group) t.group.release();
    task = null;
    t.phase = "failed";
    t.reject(described);
    report(described, t.SceneClass);
    startNext();
}

/* Whether the loading screen shows: loading has lasted longer than loadingDelay. */
function loadingVisible(t) {
    return t.shown > settings.loadingDelay;
}

/* Runs the switch; an exception of the game ends the request and is thrown on. */
function applyChecked(t) {
    try {
        return apply(t);
    } catch (error) {
        abort(t, error);
        throw error;
    }
}

function entered(t, result) {
    if (!isThenable(result)) {
        t.phase = "in";
        t.time = 0;
        if (t.inTime === 0) finish(t);
        return;
    }
    t.phase = "entering";
    Promise.resolve(result).then(() => {
        if (task !== t) return;
        t.phase = "in";
        t.time = 0;
        if (t.inTime === 0) finish(t);
    }, error => {
        if (task !== t) return;
        abort(t, error);
        report(error, t.SceneClass);
    });
}

/* Advances the current request by `dt` real seconds. */
function tick(dt) {
    const t = task;
    if (!t) return;
    t.time += dt;
    switch (t.phase) {
    case "out":
        if (t.time < t.outTime) return;
        t.phase = t.kind === "pop" ? "switch" : "load";
        t.time = 0;
        if (t.kind === "go" && t.options.unloadFirst) {
            try {
                clearStack();
            } catch (error) {
                abort(t, error);
                throw error;
            } finally {
                collectGarbage();
            }
            try {
                t.group = new AssetGroup(t.manifest, t.SceneClass.root ?? "", t.where);
            } catch (error) {
                fail(t, error);
                return;
            }
        }
        if (t.phase === "switch") {
            applyChecked(t);
            entered(t, undefined);
            return;
        }
        /* falls through: the assets may all be cached */
    case "load":
        if (t.group.failed) {
            fail(t, t.group.error);
            return;
        }
        if (!t.group.done) {
            t.shown += dt;
            t.loadTime += dt;
            const timeout = t.options.timeout ?? (settings.loadTimeout > 0 ? settings.loadTimeout : undefined);
            if (timeout !== undefined && t.loadTime >= timeout) {
                fail(t, timeoutError(`not loaded after ${timeout} s`, t.group.pending()));
                return;
            }
            if (!t.warned && settings.slowLoadWarning > 0 && t.loadTime >= settings.slowLoadWarning) {
                t.warned = true;
                const list = t.group.pending().map(item => `${item.kind} '${item.path}'`).join(", ");
                console.log(`${t.where}(${t.SceneClass.name}): still loading after ` +
                    `${settings.slowLoadWarning} s: ${list}`);
            }
            return;
        }
        /* A loading screen that appeared stays long enough to be read. */
        if (loadingVisible(t) && t.shown - settings.loadingDelay < settings.minLoadingTime) {
            t.shown += dt;
            return;
        }
        t.phase = "enter";
        t.time = 0;
        entered(t, applyChecked(t));
        return;
    case "entering":
        t.shown += dt;
        return;
    case "in":
        if (t.time >= t.inTime) finish(t);
        return;
    default:
        return;
    }
}

function finish(t) {
    if (task !== t) return;
    t.phase = "done";
    task = null;
    t.resolve(t.kind === "pop" ? stack.length > 0 ? stack[stack.length - 1].scene : null : t.scene);
    startNext();
}

/* Whether the scenes are frozen: fading out, loading, or waiting for an async enter(). */
function frozen() {
    return task !== null && (task.phase === "out" || task.phase === "load" ||
        task.phase === "entering" || task.phase === "switch");
}

function overlayAlpha() {
    const t = task;
    if (!t) return 0;
    if (t.phase === "out") return t.outTime > 0 ? smooth(t.time / t.outTime) : 1;
    if (t.phase === "in") return t.inTime > 0 ? 1 - smooth(t.time / t.inTime) : 0;
    if (t.phase === "load" || t.phase === "entering" || t.phase === "enter")
        return t.outTime > 0 ? 1 : 0;
    return 0;
}

function drawOverlay() {
    const t = task;
    if (!t) return;
    const amount = overlayAlpha();
    const loading = (t.phase === "load" || t.phase === "entering") && loadingVisible(t);
    if (amount <= 0 && !loading) return;
    screenSpace(() => {
        if (amount > 0) {
            const { width, height } = Screen.getMode();
            const transition = t.options.transition;
            const draw = isCustomTransition(transition) ? transition.draw.bind(transition) :
                transitions.get(transition);
            if (draw) draw(amount, {
                phase: t.phase === "out" ? "out" : t.phase === "in" ? "in" : "hold",
                color: t.color, width, height, direction: t.options.direction ?? "left",
            });
        }
        if (loading && settings.loadingScreen)
            settings.loadingScreen(t.group ? t.group.progress : 1, {
                scene: t.SceneClass, loaded: t.group ? t.group.loaded : 0,
                total: t.group ? t.group.total : 0,
            });
    });
}

/*
 * The base class of scenes. Subclasses override what they need; the manager
 * calls enter() once the assets are loaded, then update() and draw() every
 * frame while the scene runs, pause()/resume() around scenes pushed above
 * it, and exit() when it leaves.
 */
export class Scene {
    constructor(params) {
        /* What go()/push() passed as options.params. */
        this.params = params;
        /* The loaded assets, as the manifest names them; set before enter(), null after exit(). */
        this.assets = null;
        this[CLEANUPS] = [];
        this[GROUPS] = [];
    }

    enter(assets, params) {}
    update(dt) {}
    draw(alpha) {}
    pause() {}
    resume(result) {}
    exit() {}

    /*
     * Runs `fn` when the scene leaves, after exit(), last registered first:
     * for what the scene started (tweens, sprites, Loop systems, music).
     * Returns `fn`.
     */
    defer(fn) {
        if (typeof fn !== "function")
            throw new TypeError("Scene.defer expects a function");
        this[CLEANUPS].push(fn);
        return fn;
    }

    /*
     * More assets while the scene runs (a level's next area): a group,
     * released when the scene leaves. Paths are relative to the class's root.
     */
    acquire(manifest, options) {
        const { root, timeout } = readLoadOptions(options, "Scene.acquire", this.constructor.root ?? "");
        const group = new AssetGroup(manifest, root, "Scene.acquire", timeout);
        this[GROUPS].push(group);
        return group;
    }

    /* ---- The manager ---- */

    /*
     * Replaces every scene with a new one: loads its assets (during the
     * fade-out), exits the old scenes and releases their assets, then
     * enters the new one. Resolves with the new scene once it runs.
     */
    static go(SceneClass, options) {
        checkSceneClass(SceneClass, "Scene.go");
        return request("go", SceneClass, options, "Scene.go");
    }

    /* A scene over the current one, which pauses (a pause menu, a dialog). */
    static push(SceneClass, options) {
        checkSceneClass(SceneClass, "Scene.push");
        return request("push", SceneClass, options, "Scene.push");
    }

    /* Leaves the top scene; the one below resumes with options.result. */
    static pop(options) {
        return request("pop", null, options, "Scene.pop");
    }

    /*
     * Replaces only the top scene (the one below stays, not resumed): the
     * next room of a level, the next step of a menu. With an empty stack it
     * is a go().
     */
    static replace(SceneClass, options) {
        checkSceneClass(SceneClass, "Scene.replace");
        return request("replace", SceneClass, options, "Scene.replace");
    }

    /*
     * Starts loading a scene's assets now (the level while the title runs):
     * go() or push() then finds them loaded. The returned group is released
     * when that scene class enters, or with release().
     */
    static preload(SceneClass, params) {
        checkSceneClass(SceneClass, "Scene.preload");
        let group;
        try {
            group = new AssetGroup(manifestOf(SceneClass, params), SceneClass.root ?? "", "Scene.preload");
        } catch (error) {
            throw describe("Scene.preload", SceneClass, error);
        }
        if (!preloads.has(SceneClass)) preloads.set(SceneClass, []);
        preloads.get(SceneClass).push(group);
        return group;
    }

    /*
     * A named transition for go/push/pop/replace: draw(amount, info) covers
     * `amount` (0..1) of the screen in screen space; info has phase ("out",
     * "hold" while loading, "in"), color, width, height and direction.
     */
    static defineTransition(name, draw) {
        if (typeof name !== "string" || name === "" || name === "none")
            throw new TypeError("Scene.defineTransition: the name must be a string other than \"none\"");
        if (typeof draw !== "function")
            throw new TypeError("Scene.defineTransition expects a draw(amount, info) function");
        transitions.set(name, draw);
    }

    /* Starts the Loop with the manager's update and draw, and goes to the first scene. */
    static run(SceneClass, options, loopOptions) {
        checkSceneClass(SceneClass, "Scene.run");
        if (Loop.isRunning())
            throw new TypeError("Scene.run: the Loop is already running; call Scene.update(dt) and " +
                "Scene.draw(alpha) from its handlers instead");
        const first = Scene.go(SceneClass, options);
        Loop.run({ update: dt => Scene.update(dt), draw: alpha => Scene.draw(alpha) }, loopOptions);
        return first;
    }

    /* For custom loops: updates the running scenes (inside Loop.run(), Scene.run() does it). */
    static update(dt) {
        if (!Loop.isRunning()) {
            /* No Loop.run(): its systems do not run, so the manager's time is the game's. */
            pumpAssets(dt);
            tick(dt);
        }
        if (pendingError) {
            const error = pendingError;
            pendingError = null;
            throw error;
        }
        if (frozen()) return;
        /* The top, and the scenes below it that asked to keep updating. */
        let first = stack.length - 1;
        while (first > 0 && stack[first].updateBelow) first--;
        for (let i = Math.max(first, 0); i < stack.length; i++) stack[i].scene.update(dt);
    }

    /* For custom loops: draws the visible scenes, then the transition and the loading screen. */
    static draw(alpha) {
        const t = task;
        const waiting = t && (t.phase === "load" || t.phase === "entering");
        /*
         * While the next scene loads the old ones stay on screen, frozen,
         * unless a fade already covered them or (go) the loading screen
         * replaced them.
         */
        const hidden = waiting && (t.outTime > 0 || (t.kind === "go" && loadingVisible(t)));
        if (!hidden) {
            let last = stack.length - 1;
            /* A scene whose async enter() is running is not drawn yet. */
            if (t && t.phase === "entering") last--;
            let first = last;
            while (first > 0 && stack[first].drawBelow) first--;
            for (let i = Math.max(first, 0); i <= last; i++) stack[i].scene.draw(alpha);
        }
        drawOverlay();
    }

    /* The top scene, or null. */
    static get current() {
        return stack.length > 0 ? stack[stack.length - 1].scene : null;
    }
    /* Every scene, bottom first. */
    static get stack() { return stack.map(entry => entry.scene); }
    /* True while a go, push, pop or replace is in progress. */
    static get busy() { return task !== null || queue.length > 0; }
    /* Loading progress of the scene being loaded, 0..1; 1 when nothing loads. */
    static get progress() { return task && task.group ? task.group.progress : 1; }

    /*
     * Draws the loading screen: (progress, { scene, loaded, total }) in
     * screen space, while a scene loads longer than its transition. null
     * shows nothing. The default is a progress bar.
     */
    static get loadingScreen() { return settings.loadingScreen; }
    static set loadingScreen(fn) {
        if (fn !== null && typeof fn !== "function")
            throw new TypeError("Scene.loadingScreen must be a function or null");
        settings.loadingScreen = fn;
    }
    /* Seconds of loading before the loading screen shows (default 0.15). */
    static get loadingDelay() { return settings.loadingDelay; }
    static set loadingDelay(seconds) {
        if (typeof seconds !== "number" || !(seconds >= 0) || seconds === Infinity)
            throw new RangeError("Scene.loadingDelay must be a finite number of seconds");
        settings.loadingDelay = seconds;
    }
    /*
     * Seconds of loading after which the pending assets of the scene are
     * logged once, naming each (default 10; 0 never).
     */
    static get slowLoadWarning() { return settings.slowLoadWarning; }
    static set slowLoadWarning(seconds) {
        if (typeof seconds !== "number" || !(seconds >= 0) || seconds === Infinity)
            throw new RangeError("Scene.slowLoadWarning must be a finite number of seconds");
        settings.slowLoadWarning = seconds;
    }
    /*
     * Seconds after which a scene that has not loaded fails like a missing
     * file (onError, or the next update), listing what was still loading;
     * options.timeout overrides it per request. Default 0: never.
     */
    static get loadTimeout() { return settings.loadTimeout; }
    static set loadTimeout(seconds) {
        if (typeof seconds !== "number" || !(seconds >= 0) || seconds === Infinity)
            throw new RangeError("Scene.loadTimeout must be a finite number of seconds");
        settings.loadTimeout = seconds;
    }
    /* Seconds a loading screen that appeared stays at least (default 0.3: no flash). */
    static get minLoadingTime() { return settings.minLoadingTime; }
    static set minLoadingTime(seconds) {
        if (typeof seconds !== "number" || !(seconds >= 0) || seconds === Infinity)
            throw new RangeError("Scene.minLoadingTime must be a finite number of seconds");
        settings.minLoadingTime = seconds;
    }
    /*
     * Called with (error, SceneClass) when a scene cannot load; the current
     * scene stays. Without it the error is thrown from the next update.
     */
    static get onError() { return settings.onError; }
    static set onError(fn) {
        if (fn !== null && typeof fn !== "function")
            throw new TypeError("Scene.onError must be a function or null");
        settings.onError = fn;
    }

    /*
     * Leaves every scene without transitions and releases their assets and
     * preloads: for tests and for going back to a launcher.
     */
    static reset() {
        for (const pending of queue.splice(0)) pending.reject(new Error("Scene.reset"));
        if (task) {
            const t = task;
            task = null;
            if (t.group && !stack.some(entry => entry.group === t.group)) t.group.release();
            t.reject(new Error("Scene.reset"));
        }
        pendingError = null;
        if (managerSystem) {
            Loop.removeSystem(managerSystem);
            managerSystem = null;
        }
        const steps = [() => clearStack()];
        for (const SceneClass of [...preloads.keys()]) steps.push(() => releasePreloads(SceneClass));
        try {
            runAll(steps);
        } finally {
            collectGarbage();
        }
    }
}

Scene.Assets = Assets;
Scene.AssetGroup = AssetGroup;
