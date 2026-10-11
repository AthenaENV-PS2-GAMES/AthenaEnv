/*
 * Assets3D module: the meshes, textures3d and gltf kinds in Scene.Assets,
 * reference counting with disposal, budgeted loading between frames and
 * errors. Host test (tests/js/run.sh): the files come from tests/host/3d.
 */
import * as Assets3D from "Assets3D";
import * as Model3D from "Model3D";
import * as GLTF3D from "GLTF3D";
import * as Scene3D from "Scene3D"; // GLTF3D builds Scene3D nodes (the runtime bootstrap imports it)
import * as Animation3D from "Animation3D";
void Scene3D;
void Animation3D;
import { Assets } from "Scene";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
globalThis.Model3D = globalThis.Model3D || Model3D;
globalThis.GLTF3D = globalThis.GLTF3D || GLTF3D;
const ROOT = typeof globalThis.__nativeDraws === "function" ? "../tests/host/3d" : "3d";
const pump = async (n = 10) => { for (let i = 0; i < n; i++) { Assets3D.update(); Assets.update(0); await null; } };
const yieldWorker = () => new Promise(resolve => setTimeout(resolve, 1));
async function settle(group) {
    for (let i=0; !group.done && !group.failed && i<20000; i++) {
        Assets3D.update(); Assets.update(0); await yieldWorker();
    }
    return group.ready;
}

async function main() {
    check("kinds", Assets3D.KINDS.join() === "meshes,textures3d,gltf");
    Assets3D.setBudget(0);   // one load per update
    const group = Assets.acquire({
        meshes: { tri: "triangle.obj", lit: { path: "triangle.obj", material: { shading: Model3D.DIFFUSE, baseColor: [1, 0, 0, 1] } } },
        textures3d: { checker: { path: "checker.png", filter: 1, wrap: 3 } },
    }, ROOT);
    await null;
    check("queued, not loaded synchronously", Assets3D.stats().queued === 3 && !group.done);
    Assets3D.update();
    check("budget 0 still runs one load per update", Assets3D.stats().queued === 2);
    await settle(group); // the texture is decoded by a worker: needs real time, not just microtasks
    const a = group.assets;
    check("loaded values", a.meshes.tri instanceof Model3D.Mesh && a.meshes.lit instanceof Model3D.Mesh &&
        a.textures3d.checker instanceof Model3D.Texture && a.textures3d.checker.wrap === 3);
    check("different materials are different assets", a.meshes.tri !== a.meshes.lit);
    const second = Assets.acquire({ meshes: { tri: "triangle.obj" } }, ROOT);
    await second.ready;
    check("shared by path", second.assets.meshes.tri === a.meshes.tri);
    group.release();
    await pump(2);
    check("still held by the second group", (() => { try { a.meshes.tri.createInstance().dispose(); return true; } catch (_) { return false; } })());
    check("material-only mesh freed", (() => { try { a.meshes.lit.createInstance(); return false; } catch (_) { return true; } })());
    second.release();
    await pump(2);
    check("freed with the last holder", (() => { try { a.meshes.tri.createInstance(); return false; } catch (_) { return true; } })());

    const bad = Assets.acquire({ meshes: { missing: "nope.obj" } }, ROOT);
    await pump();
    let error = null; try { await bad.ready; } catch (e) { error = e; }
    check("load errors reject the group", error !== null && Assets3D.stats().failed >= 1);
    bad.release();

    if (globalThis.GLTF3D) {
        const gl = Assets.acquire({ gltf: { tri: "triangle.gltf" } }, ROOT);
        await settle(gl);
        check("gltf asset", gl.assets.gltf.tri.root && Array.isArray(gl.assets.gltf.tri.nodes));
        gl.release();
    } else {
        const gl = Assets.acquire({ gltf: { tri: "triangle.gltf" } }, ROOT);
        let e2 = null; try { await settle(gl); } catch (e) { e2 = e; }
        check("gltf without GLTF3D explains", e2 !== null && /GLTF3D/.test(String(e2.message || e2)));
        gl.release();
    }
    const cubePath = "models/lit_cube.glb";
    const held = Assets.acquire({ meshes: { cube: cubePath } });
    const shared = Assets.acquire({ meshes: { cube: cubePath } });
    Assets3D.update();
    check("GLB read starts and yields before parse", !held.done && Assets3D.stats().reading === 1 && Assets3D.pending()[0].phase === "reading");
    held.release();
    await settle(shared);
    check("another holder keeps read alive", shared.assets.meshes.cube instanceof Model3D.Mesh);
    check("job-memory phase metrics", Assets3D.stats().lastLoad.mode === (typeof Model3D.beginMemory === "function" ? "job-memory-steps" : "job-memory") && Assets3D.stats().lastLoad.bytes > 0);
    shared.release(); await pump(2);

    const readOriginal = globalThis.Thread;
    const gltfOriginal = globalThis.GLTF3D;
    const bytes = await readOriginal.readFileAsync("models/arm.glb");
    let state = "running", cancels = 0, constructions = 0;
    globalThis.Thread = { readFileAsync() { return {
        poll() { return { state, result: bytes, bytesDone: state === "done" ? bytes.byteLength : 0, bytesTotal: bytes.byteLength }; },
        cancel() { cancels++; },
    }; } };
    globalThis.GLTF3D = { loadMemory(...args) { constructions++; return GLTF3D.loadMemory(...args); } };
    try {
        const queued = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
        queued.release(); await pump(2);
        check("cancel before submission", constructions === 0 && cancels === 0 && !Assets3D.pending().length);
        const reading = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
        Assets3D.update(); Assets3D.update();
        check("poll does not construct while worker runs", constructions === 0);
        reading.release(); await pump(2);
        check("cancel running read", constructions === 0 && cancels === 1 && Assets3D.stats().reading === 0);
        state = "done";
        const finishedRead = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
        Assets3D.update(); finishedRead.release(); await pump(2);
        check("cancel finished read before conversion", constructions === 0 && cancels === 2);
        const retry = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
        await pump(); await retry.ready;
        check("reacquire after cancellation constructs once", constructions === 1 && retry.assets.gltf.arm.nodes.length === 3);
        retry.release(); await pump(2);
    } finally { globalThis.Thread = readOriginal; globalThis.GLTF3D = gltfOriginal; }

    const realCancel = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
    Assets3D.update(); realCancel.release(); await yieldWorker(); await pump(2); std.gc();
    check("real read cancellation empties queue", !Assets3D.pending().length && Assets3D.stats().cancelled >= 4);
    if (globalThis.GLTF3D && typeof GLTF3D.beginMemory === "function") {
        // Resumable construction: budget 0 runs one unit per update().
        const before = Assets3D.stats();
        const stepped = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
        let constructCalls = 0, sawPhase = new Set(), maxTotal = 0, previous = -1, monotonic = true;
        for (let i = 0; i < 4000 && !stepped.done; i++) {
            Assets3D.update(); await yieldWorker();
            const p = Assets3D.pending()[0];
            if (p && p.phase === "construct") {
                constructCalls++; sawPhase.add(p.constructPhase); maxTotal = Math.max(maxTotal, p.unitsTotal);
                if (p.unitsDone < previous) monotonic = false; previous = p.unitsDone;
            }
        }
        await settle(stepped);
        check("resumable GLB construction spans several updates", constructCalls >= 3 && stepped.assets.gltf.arm.nodes.length === 3);
        check("construction progress is monotonic and counted", monotonic && maxTotal >= 5 && sawPhase.has("nodes"));
        const after = Assets3D.stats();
        check("phase stats recorded", after.lastLoad.mode === "job-memory-steps" && after.lastLoad.steps >= 5 &&
            ["begin", "parse", "validate", "nodes", "links", "finish"].every(n => after.phases[n] && after.phases[n].count > 0) &&
            after.phases.parse.maxMs <= after.maxConstructionMs);
        check("stepped completion counted", after.completed === before.completed + 1);
        // Cancel while the scene is partially built; a second holder keeps a shared load alive.
        // maxBytes is part of the cache identity: these loads must not reuse `stepped`.
        const doomed = Assets.acquire({ gltf: { arm: { path: "models/arm.glb", maxBytes: 8 << 20 } } });
        for (let i = 0; i < 4000 && !(Assets3D.pending()[0]?.constructPhase === "nodes"); i++) { Assets3D.update(); await yieldWorker(); }
        check("cancel target is mid-construction", Assets3D.pending()[0]?.constructPhase === "nodes");
        doomed.release(); await pump(2);
        check("cancel mid-construction empties queue", !Assets3D.pending().length && Assets3D.stats().cancelled === after.cancelled + 1);
        const sharedSpec = { gltf: { arm: { path: "models/arm.glb", maxBytes: 4 << 20 } } };
        const holderA = Assets.acquire(sharedSpec), holderB = Assets.acquire(sharedSpec);
        for (let i = 0; i < 4000 && !(Assets3D.pending()[0]?.constructPhase === "nodes"); i++) { Assets3D.update(); await yieldWorker(); }
        holderA.release(); await settle(holderB);
        check("another holder keeps a stepped load alive", holderB.assets.gltf.arm.nodes.length === 3);
        holderB.release(); stepped.release(); await pump(2);
        Assets3D.setBudget(8);
        const timed = Assets.acquire({ gltf: { arm: "models/arm.glb" } });
        await settle(timed);
        check("default budget completes resumable loads", timed.assets.gltf.arm.nodes.length === 3); timed.release();
        Assets3D.setBudget(0);
        if (typeof Model3D.beginMemory === "function") {
            const before = Assets3D.stats().phases.build?.count ?? 0;
            const mesh = Assets.acquire({ meshes: { cube: { path: "models/lit_cube.glb", maxBytes: 2 << 20 } } });
            await settle(mesh);
            const st = Assets3D.stats();
            check("mesh GLB builds in steps", mesh.assets.meshes.cube instanceof Model3D.Mesh && st.lastLoad.mode === "job-memory-steps" &&
                st.phases.build.count > before && !st.phases.copy);
            mesh.release(); await pump(2);
        }
        {
            // A budget lets one update() run several units; stepped:false builds a GLB atomically.
            Assets3D.setBudget(8); Assets3D.resetStats();
            const g = Assets.acquire({ gltf: { arm: { path: "models/arm.glb", maxBytes: 3 << 20 } } });
            let calls = 0; for (let i = 0; i < 4000 && !g.done; i++) { Assets3D.update(); Assets.update(0); calls++; await yieldWorker(); }
            await settle(g);
            const st = Assets3D.stats(), ph = st.phases;
            check("units run under a budget share update() calls", st.lastLoad.steps > 3 && ph.nodes.count > 3 && ph.nodes.maxMs > 0 && ph.nodes.maxMs <= ph.nodes.totalMs && !("copy" in ph));
            g.release(); await pump(2);
            Assets3D.setBudget(0);
            const atomic = Assets.acquire({ gltf: { arm: { path: "models/arm.glb", maxBytes: 5 << 20, stepped: false } } });
            await settle(atomic);
            check("stepped:false builds the GLB in one call", Assets3D.stats().lastLoad.mode === "job-memory" && atomic.assets.gltf.arm.nodes.length === 3);
            atomic.release(); await pump(2);
            Assets3D.setStepped(false);
            const viaDefault = Assets.acquire({ meshes: { cube: { path: "models/lit_cube.glb", maxBytes: 5 << 20 } } });
            await settle(viaDefault);
            check("setStepped(false) changes the default", Assets3D.stats().lastLoad.mode === "job-memory" && viaDefault.assets.meshes.cube instanceof Model3D.Mesh);
            viaDefault.release(); await pump(2);
            const gltfAtomic = Assets.acquire({ gltf: { bend: { path: ROOT+"/ext_bend.gltf", stepped: true } } });
            await settle(gltfAtomic); check("per-asset stepped:true overrides the default", Assets3D.stats().lastLoad.mode === "job-memory-steps"); gltfAtomic.release(); await pump(2);
            Assets3D.setStepped(true);
            let badStepped = null; try { Assets3D.setStepped("no"); } catch (e) { badStepped = e; }
            check("stepped must be a boolean", badStepped instanceof TypeError);
            let garbage = null, garbageError = null;
            try { garbage = Assets.acquire({ meshes: { x: { path: "models/lit_cube.glb", stepped: 1 } } }); await settle(garbage); } catch (e) { garbageError = e; }
            check("invalid stepped option rejects", garbageError !== null); garbage?.release();
        }
        const bad = Assets.acquire({ gltf: { x: ROOT + "/memory_external_buffer.glb" } });
        let badError; try { await settle(bad); } catch (e) { badError = e; }
        check("stepped dependency error rejects without leaking the loader", badError && !Assets3D.pending().length); bad.release();
    }
    // Texture decoding on a job-pool worker: only the adoption of the pixels runs in update().
    {
        const before = Assets3D.stats();
        const tex = Assets.acquire({ textures3d: { c: { path: "checker.png", filter: 1, wrap: 3, upload: true } } }, ROOT);
        Assets3D.update();
        check("texture decode starts on a worker", !tex.done && Assets3D.pending()[0]?.phase === "reading");
        await settle(tex);
        const t = tex.assets.textures3d.c, last = Assets3D.stats().lastLoad;
        check("decoded texture is adopted", t instanceof Model3D.Texture && t.width > 0 && last.mode === "job-decode" && Assets3D.stats().completed === before.completed + 1);
        const sync = Assets.acquire({ textures3d: { c: { path: "checker.png", filter: 0, wrap: 3, asyncRead: false } } }, ROOT);
        await settle(sync);
        check("asyncRead:false loads the texture in update()", Assets3D.stats().lastLoad.mode === "path" && sync.assets.textures3d.c.width === t.width);
        tex.release(); sync.release(); await pump(2);
        const cancelled = Assets3D.stats().cancelled;
        const gone = Assets.acquire({ textures3d: { x: { path: "checker.png", wrap: 1 } } }, ROOT);
        Assets3D.update(); gone.release(); await pump(2); await yieldWorker();
        check("cancelled texture decode empties the queue", !Assets3D.pending().length && Assets3D.stats().cancelled === cancelled + 1);
        for (const [label, spec] of [["missing", { path: "nothing.png" }], ["too large", { path: "checker.png", maxBytes: 8 }], ["not an image", { path: "../tests/host/3d/invalid.gltf" }]]) {
            let error = null, group = null;
            try { group = Assets.acquire({ textures3d: { x: spec } }, ROOT); await settle(group); } catch (e) { error = e; }
            check("texture " + label + " rejects", error !== null && !Assets3D.pending().length); group?.release();
        }
        let range = null, rangeGroup = null; try { rangeGroup = Assets.acquire({ textures3d: { x: { path: "checker.png", maxBytes: 0 } } }, ROOT); await settle(rangeGroup); } catch (e) { range = e; }
        rangeGroup?.release();
        check("texture maxBytes is validated", range instanceof RangeError);
    }
    const tooLarge = Assets.acquire({ gltf: { arm: { path: "models/arm.glb", maxBytes: 16 } } });
    let limitError; try { await settle(tooLarge); } catch (e) { limitError=e; }
    check("job size limit rejects with origin", limitError && /arm.glb/.test(String(limitError))); tooLarge.release();
    const missing = Assets.acquire({ gltf: { arm: "models/missing.glb" } });
    let missingError; try { await settle(missing); } catch(e) { missingError=e; }
    check("missing read rejects", missingError instanceof Error); missing.release();
    // External files are read explicitly on jobs: a GLB with an image, and .gltf files with a BIN and a PNG.
    const external = Assets.acquire({ gltf: { arm: ROOT+"/memory_external_image.glb" } });
    await settle(external);
    check("external image of a GLB is resolved", external.assets.gltf.arm.nodes.length === 1 && Assets3D.stats().lastLoad.dependencies === 1);
    external.release();
    if (typeof Model3D.beginMemory === "function") {
        const ext = Assets.acquire({ gltf: { bend: ROOT+"/ext_bend.gltf" }, meshes: { cube: ROOT+"/ext_textured.gltf" } });
        let sawDependencies = false, sawPending = false;
        for (let i = 0; i < 4000 && !ext.done; i++) {
            Assets3D.update(); Assets.update(0); await yieldWorker();
            const p = Assets3D.pending()[0]; if (p?.phase === "dependencies") { sawDependencies = true; sawPending = sawPending || p.constructPhase === "dependencies"; }
        }
        await settle(ext);
        check("gltf with external BIN (percent-encoded name) and mesh with BIN+PNG load", ext.assets.gltf.bend.nodes.length === 3 &&
            ext.assets.meshes.cube instanceof Model3D.Mesh && sawDependencies && sawPending);
        check("dependency stats", Assets3D.stats().lastLoad.dependencyWaitMs >= 0 && Assets3D.stats().phases.dependencies.count > 0);
        ext.release(); await pump(2);
        const gone = Assets.acquire({ gltf: { bend: ROOT+"/ext_bend.gltf" } });
        for (let i = 0; i < 4000 && Assets3D.pending()[0]?.phase !== "dependencies"; i++) { Assets3D.update(); await yieldWorker(); }
        check("cancel while reading a dependency", Assets3D.pending()[0]?.phase === "dependencies");
        const cancelled = Assets3D.stats().cancelled; gone.release(); await pump(2); await yieldWorker();
        check("cancelled dependency read empties the queue", !Assets3D.pending().length && Assets3D.stats().cancelled === cancelled + 1);
        const limited = Assets.acquire({ gltf: { bend: { path: ROOT+"/ext_bend.gltf", maxBytes: 1024 } } });
        let limitedError; try { await settle(limited); } catch (e) { limitedError = e; }
        check("per-file limit applies to dependencies", limitedError && !Assets3D.pending().length); limited.release();
    }

    const t1 = Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([0xffffffff])});
    const t2 = Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([0xff0000ff])});
    const texturedPath = ROOT+"/memory_textured.glb";
    const materialGroup = Assets.acquire({ meshes: {
        a: { path: texturedPath, material: { texture: t1, shading: Model3D.UNLIT } },
        b: { path: texturedPath, material: { shading: Model3D.UNLIT, texture: t2 } },
        c: { path: texturedPath, material: { shading: Model3D.UNLIT, texture: t1 } },
    } });
    await settle(materialGroup);
    check("texture identity included in material cache key", materialGroup.assets.meshes.a !== materialGroup.assets.meshes.b);
    check("material property order does not change identity", materialGroup.assets.meshes.a === materialGroup.assets.meshes.c);
    materialGroup.release(); await pump(2); t1.dispose(); t2.dispose();

    const textureGroup = Assets.acquire({ textures3d: { t: ROOT+"/checker.png" } });
    await settle(textureGroup);
    let uploads = 0;
    const texture = textureGroup.assets.textures3d.t, nativeUpload = texture.upload;
    texture.upload = function() { uploads++; return nativeUpload.call(this); };
    const uploaded = Assets.acquire({ textures3d: { t: { path: ROOT+"/checker.png", upload: true } } });
    await uploaded.ready;
    check("cached texture upload request is honored", uploaded.assets.textures3d.t === texture && uploads === 1);
    uploaded.release(); textureGroup.release(); await pump(2);
    check("all loads drained", !Assets3D.pending().length && Assets3D.stats().queued === 0);
    Assets3D.setBudget(8); std.gc();
}
main().catch(e => { failed++; console.log("[FAIL] " + e + "\n" + (e.stack || "")); }).then(() => {
    console.log(`Result: ${passed} passed, ${failed} failed`);
    if (!failed) console.log("Assets3D module test passed");
});
