/*
 * Assets3D: 3D asset kinds for Scene.Assets manifests.
 *
 *   meshes:     Model3D.Mesh      ("ship.obj" or { path, material })
 *   textures3d: Model3D.Texture   ({ path, filter, wrap, upload })
 *   gltf:       GLTF3D.Asset      ({ path, material }; needs GLTF3D)
 *
 * Scene.Assets counts references by kind and path, so a mesh two scenes use
 * loads once and is disposed when the last holder releases it. Model3D and
 * GLTF3D parse synchronously; to keep a loading screen moving, loads are
 * queued and run between frames within budgetMs (at least one per frame),
 * by a Loop system or by Assets3D.update() in games without Loop.run().
 */
import { Assets } from "Scene";

const g = globalThis;
const queue = [];
let system = null;
let completed = 0, failed = 0, lastMs = 0;

/** Milliseconds of loading per frame (at least one asset per frame runs). Default 8. */
export let budgetMs = 8;
export function setBudget(ms) {
    if (typeof ms !== "number" || !(ms >= 0)) throw new RangeError("Assets3D budget must be a non-negative number");
    budgetMs = ms;
}

function optional(name, what) {
    const module = g[name];
    if (module === undefined) throw new Error(`${what} needs the ${name} module, which is not in this build`);
    return module;
}

function material(spec, where) {
    const m = spec.material;
    if (m === undefined) return undefined;
    if (m === null || typeof m !== "object") throw new TypeError(`${where}: material must be an object`);
    const out = { ...m };
    if (Array.isArray(m.baseColor)) out.baseColor = new Float32Array(m.baseColor);
    return out;
}
function materialKey(spec) {
    const m = spec.material;
    if (!m) return "";
    const plain = { ...m };
    if (m.baseColor) plain.baseColor = Array.from(m.baseColor);
    if (m.texture) plain.texture = "[texture]";
    return "#" + JSON.stringify(plain);
}

function schedule(run) {
    return new Promise((resolve, reject) => {
        queue.push({ run, resolve, reject });
        useSystem();
    });
}

function useSystem() {
    if (system || !g.Loop || typeof g.Loop.addSystem !== "function") return;
    system = g.Loop.addSystem({
        name: "assets3d",
        priority: -100,
        realTime: true,
        preUpdate() { update(); },
    });
}

/**
 * Runs queued loads for up to budgetMs (at least one). Called by the Loop
 * system; games without Loop.run() call it once per frame. Returns how many
 * loads ran.
 */
export function update() {
    const start = Date.now();
    let ran = 0;
    while (queue.length && (ran === 0 || Date.now() - start < budgetMs)) {
        const job = queue.shift();
        ran++;
        try { job.resolve(job.run()); completed++; }
        catch (e) { failed++; job.reject(e); }
    }
    if (ran) lastMs = Date.now() - start;
    if (!queue.length && system) { g.Loop.removeSystem(system); system = null; }
    return ran;
}

function dispose(value) { if (value && typeof value.dispose === "function") value.dispose(); }

const kinds = {
    meshes: {
        load(path, spec) {
            const Model3D = optional("Model3D", "Assets3D meshes");
            const m = material(spec, "Assets3D meshes");
            return schedule(() => Model3D.load(path, m));
        },
        free(mesh) { dispose(mesh); },
        key(path, spec) { return path + materialKey(spec); },
    },
    textures3d: {
        load(path, spec) {
            const Model3D = optional("Model3D", "Assets3D textures3d");
            return schedule(() => {
                const texture = Model3D.Texture.load(path, spec.filter ?? 0, spec.wrap ?? 0);
                if (spec.upload) texture.upload();
                return texture;
            });
        },
        free(texture) { dispose(texture); },
        key(path, spec) { return `${path}@${spec.filter ?? 0}/${spec.wrap ?? 0}`; },
    },
    gltf: {
        load(path, spec) {
            const GLTF3D = optional("GLTF3D", "Assets3D gltf");
            const m = material(spec, "Assets3D gltf");
            return schedule(() => GLTF3D.load(path, m));
        },
        /* Nodes and clips are native references: let go of all of them. */
        free(asset) {
            for (const clip of Object.values(asset.clips || {})) dispose(clip);
            for (const node of asset.nodes || []) dispose(node);
            if (asset.root) { if (asset.root.detach) asset.root.detach(); dispose(asset.root); }
        },
        key(path, spec) { return path + materialKey(spec); },
    },
};
for (const [kind, loader] of Object.entries(kinds)) Assets.define(kind, loader);

/** Queue state, e.g. for Debug.watch(). */
export function stats() {
    return { queued: queue.length, completed, failed, lastFrameMs: lastMs, budgetMs };
}

/** The kinds this module adds. */
export const KINDS = Object.keys(kinds);
