import * as Model3D from "Model3D";
import * as GLTF3D from "GLTF3D";
import * as Scene3D from "Scene3D";
import * as Animation3D from "Animation3D";
void Scene3D;
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function rejects(fn, label) { let error; try { fn(); } catch(e) { error=e; } assert(error instanceof Error, label); }
function free(asset) {
    for (const c of Object.values(asset.clips)) c.dispose();
    for (const n of asset.nodes) n.dispose(); asset.root.dispose();
}
const HOST = typeof globalThis.__nativeDraws === "function";
async function main() {
    for (const name of ["lit_cube", "arm", "bend", "morph"]) {
        const path = `models/${name}.glb`;
        const buffer = await Thread.readFileAsync(path), bytes = new Uint8Array(buffer);
        const padded = new Uint8Array(bytes.length+3); padded.set(bytes,1);
        const mem = GLTF3D.loadMemory(padded.subarray(1,bytes.length+1), "unavailable:/source.glb");
        const disk = GLTF3D.load(path);
        padded.fill(0); // no returned handle may borrow the input
        assert(mem.names.join()===disk.names.join(),name+" names");
        assert(mem.nodes.length===disk.nodes.length, name+" nodes");
        assert(mem.root.childCount===disk.root.childCount,name+" roots");
        assert(Object.keys(mem.clips).join()===Object.keys(disk.clips).join(),name+" clips");
        for (const key of Object.keys(mem.clips)) {
            assert(mem.clips[key].duration===disk.clips[key].duration,name+" clip duration");
            const player = new Animation3D.Player(mem.clips[key], mem.nodes); player.time=.5; player.dispose();
        }
        if (name === "lit_cube") {
            const mesh = Model3D.loadMemory(buffer, "cube.glb");
            const instance = mesh.createInstance(); mesh.dispose(); instance.dispose();
            assert(true,"mesh lifetime independent of source");
        }
        rejects(()=>GLTF3D.loadMemory(bytes.subarray(0,bytes.length-1),path),"truncation");
        free(mem); free(disk);
    }
    const path = HOST ? "../tests/host/3d/" : "3d/";
    for (const type of ["image","buffer"]) {
        const bytes = await Thread.readFileAsync(path+`memory_external_${type}.glb`);
        rejects(()=>GLTF3D.loadMemory(bytes,"external.glb"),"external "+type);
        rejects(()=>Model3D.loadMemory(bytes,"external.glb"),"mesh external "+type);
    }
    const sharedBytes = await Thread.readFileAsync(path+"memory_shared.glb");
    const shared=GLTF3D.loadMemory(sharedBytes,"shared.glb");
    assert(shared.nodes.length===1&&shared.nodes[0].childCount===1,"embedded texture primitives");
    free(shared);
    for (const bytes of [null,[],new Float32Array(10)])
        rejects(()=>GLTF3D.loadMemory(bytes,"bad.glb"),"invalid byte type");
    rejects(()=>GLTF3D.loadMemory(sharedBytes,""),"empty origin");
    rejects(()=>GLTF3D.loadMemory(sharedBytes,"bad\0origin"),"NUL origin");
    rejects(()=>Model3D.loadMemory(sharedBytes,"bad\0origin"),"mesh NUL origin");
    rejects(()=>GLTF3D.loadMemory(sharedBytes,"bad.glb",{shading:7}),"invalid material");
    const override=GLTF3D.loadMemory(sharedBytes,"override.glb",{shading:Model3D.DIFFUSE});
    assert(override.nodes.length===1,"material override"); free(override);
    // Resumable loader: same scene as the one-shot call; independent of the source bytes.
    for (const name of ["lit_cube", "arm", "bend", "morph"]) {
        const bytes = new Uint8Array(await Thread.readFileAsync(`models/${name}.glb`)), whole = GLTF3D.loadMemory(bytes, name);
        const copy = bytes.slice(), loader = GLTF3D.beginMemory(copy, "unavailable:/"+name+".glb");
        assert(loader instanceof GLTF3D.Loader && loader.phase==="copy" && loader.unitsTotal===0, name+" loader start");
        // The bytes are read only while the phase is "copy"; afterwards the loader owns its snapshot.
        while (loader.phase==="copy") loader.step();
        copy.fill(0xa5);
        let steps = 0, last = -1; const order = ["copy","parse","dependencies","validate","nodes","links","skins","clips","done"], seen = new Set();
        while (!loader.step()) {
            steps++; const at = order.indexOf(loader.lastPhase); assert(at>=last, name+" phases ordered"); last = at; seen.add(loader.lastPhase);
            assert(!loader.unitsTotal || loader.unitsDone<=loader.unitsTotal, name+" progress");
        }
        assert(steps>=2 && seen.has("validate"), name+" several steps");
        const asset = loader.finish();
        assert(asset.names.join()===whole.names.join() && asset.nodes.length===whole.nodes.length, name+" stepped scene");
        assert(Object.keys(asset.clips).join()===Object.keys(whole.clips).join(), name+" stepped clips");
        rejects(()=>loader.step(), name+" spent loader step"); rejects(()=>loader.finish(), name+" spent loader finish");
        assert(loader.phase==="disposed", name+" spent loader phase"); loader.dispose(); loader.dispose();
        free(asset); free(whole);
    }
    // A GLB larger than one copy chunk copies over several steps; each stays small and the result is identical.
    {
        const src = new Uint8Array(await Thread.readFileAsync("models/arm.glb")), view = new DataView(src.buffer, src.byteOffset, src.byteLength);
        const jsonLen = view.getUint32(12, true), json = JSON.parse(String.fromCharCode(...src.subarray(20, 20+jsonLen))); // ASCII JSON chunk
        const binStart = 20+jsonLen+8, binLen = view.getUint32(20+jsonLen, true), extra = 600*1024;
        json.buffers[0].byteLength += extra;
        let text = JSON.stringify(json); while (text.length % 4) text += " ";
        const jsonBytes = Uint8Array.from(text, ch => ch.charCodeAt(0)), out = new Uint8Array(12+8+jsonBytes.length+8+binLen+extra), o = new DataView(out.buffer);
        o.setUint32(0, 0x46546c67, true); o.setUint32(4, 2, true); o.setUint32(8, out.length, true);
        o.setUint32(12, jsonBytes.length, true); o.setUint32(16, 0x4e4f534a, true); out.set(jsonBytes, 20);
        const binAt = 20+jsonBytes.length; o.setUint32(binAt, binLen+extra, true); o.setUint32(binAt+4, 0x004e4942, true);
        out.set(src.subarray(binStart, binStart+binLen), binAt+8);
        const whole = GLTF3D.loadMemory(out, "padded.glb"), loader = GLTF3D.beginMemory(out, "padded.glb");
        let copySteps = 0; while (loader.phase === "copy") { loader.step(); copySteps++; }
        assert(copySteps === 3 && loader.unitsDone === 3, "copy spans several steps");
        out.fill(0x5a); // the snapshot is independent of the source
        while (!loader.step());
        const sawTotal = loader.unitsTotal, sawDone = loader.unitsDone, asset = loader.finish();
        assert(sawTotal > 3 && sawDone === sawTotal && asset.names.join() === whole.names.join() && asset.nodes.length === whole.nodes.length, "padded stepped scene");
        free(asset); free(whole);
        const abandoned = GLTF3D.beginMemory(new Uint8Array(out.buffer.slice(0)), "abandoned.glb"); abandoned.step(); // pinned source released with the loader
    }
    const early = GLTF3D.beginMemory(sharedBytes, "early.glb");
    rejects(()=>early.finish(), "finish before done"); early.step(); early.dispose(); early.dispose();
    rejects(()=>early.step(), "disposed loader"); // abandoned loaders with pending steps are collected below
    for (let i=0;i<4;i++) { const l = GLTF3D.beginMemory(sharedBytes, "gc.glb"); l.step(); }
    const badBytes = sharedBytes.slice(); new Uint8Array(badBytes)[0] ^= 0xff;
    const bad = GLTF3D.beginMemory(badBytes, "bad.glb");
    rejects(()=>{ while (!bad.step()); }, "bad magic"); rejects(()=>bad.step(), "sticky error"); rejects(()=>bad.finish(), "finish after error"); bad.dispose();
    for (const type of ["image","buffer"]) {
        const l = GLTF3D.beginMemory(await Thread.readFileAsync(path+`memory_external_${type}.glb`), "external.glb");
        rejects(()=>{ while (!l.step()); }, "loader external "+type); l.dispose();
    }
    rejects(()=>GLTF3D.beginMemory(sharedBytes,""), "loader empty origin");
    rejects(()=>GLTF3D.beginMemory(null,"x.glb"), "loader bad bytes");
    rejects(()=>GLTF3D.beginMemory(sharedBytes,"x.glb",{shading:7}), "loader invalid material");
    rejects(()=>new GLTF3D.Loader(), "loader constructor");
    const overrideLoader = GLTF3D.beginMemory(sharedBytes, "override.glb", {shading:Model3D.DIFFUSE});
    while (!overrideLoader.step()); free(overrideLoader.finish());
    // Model3D.Loader: the stepped mesh equals loadMemory; batches of units; independent of the source.
    {
        const bytes = new Uint8Array(await Thread.readFileAsync("models/lit_cube.glb")), whole = Model3D.loadMemory(bytes, "cube.glb");
        const copy = bytes.slice(), loader = Model3D.beginMemory(copy, "unavailable:/cube.glb", {shading: Model3D.DIFFUSE});
        assert(loader instanceof Model3D.Loader && loader.phase === "copy" && loader.unitsTotal > 0, "mesh loader start");
        while (loader.phase === "copy") loader.step();
        copy.fill(0xa5);
        const seen = new Set(), order = ["copy","parse","dependencies","validate","build","done"]; let calls = 0, last = -1, batched = 0;
        while (!loader.step(2)) { calls++; seen.add(loader.lastPhase); const at = order.indexOf(loader.lastPhase); assert(at >= last, "mesh phases ordered"); last = at; batched += loader.lastUnits; assert(loader.lastUnits >= 1 && loader.lastUnits <= 2, "lastUnits"); }
        assert(seen.has("build") && seen.has("parse") && calls >= 3, "mesh loader phases");
        const mesh = loader.finish();
        assert(mesh.vertexCount === whole.vertexCount, "stepped mesh vertex count");
        rejects(()=>loader.step(), "spent mesh loader"); assert(loader.phase === "disposed" && loader.lastUnits === 0, "spent mesh loader state");
        mesh.dispose(); whole.dispose();
        for (const bad of [0, 65, 1.5, "x"]) { const l = Model3D.beginMemory(bytes, "cube.glb"); rejects(()=>l.step(bad), "invalid units "+bad); l.dispose(); }
        const early = Model3D.beginMemory(bytes, "early.glb"); rejects(()=>early.finish(), "mesh finish before done"); early.step(); early.dispose(); early.dispose();
        const badBytes = bytes.slice(); badBytes[0] ^= 0xff; const badLoader = Model3D.beginMemory(badBytes, "bad.glb");
        rejects(()=>{ while (!badLoader.step()); }, "mesh bad magic"); rejects(()=>badLoader.step(), "mesh sticky error"); badLoader.dispose();
        const scene = new Uint8Array(await Thread.readFileAsync("models/arm.glb")), notMesh = Model3D.beginMemory(scene, "arm.glb");
        rejects(()=>{ while (!notMesh.step()); }, "scene is not a static mesh"); notMesh.dispose();
        rejects(()=>Model3D.beginMemory(bytes, ""), "mesh loader empty origin"); rejects(()=>Model3D.beginMemory(null, "x.glb"), "mesh loader bad bytes");
        rejects(()=>new Model3D.Loader(), "mesh loader constructor");
        for (let i = 0; i < 3; i++) Model3D.beginMemory(bytes, "gc.glb").step(); // abandoned while pinned
        const g = GLTF3D.beginMemory(bytes, "batch.glb"); while (g.phase === "copy") g.step(8); let n = 0; while (!g.step(8)) n++;
        free(g.finish());
    }
    // Explicit resolver: .gltf with external BIN/PNG and a GLB with an external image.
    {
        const load = async name => new Uint8Array(await Thread.readFileAsync(path+name));
        const drive = async (loader, expected) => {
            let waited = 0, guard = 0;
            while (!loader.step()) {
                if (loader.phase !== "dependencies") { assert(++guard < 100000, "loader progress"); continue; }
                const deps = loader.dependencies;
                assert(deps.length === expected.length && deps.every((d, i) => d.uri === expected[i]), "dependency list");
                const todo = deps.findIndex(d => !d.provided);
                if (todo < 0) continue;
                waited++; assert(loader.step() === false && loader.lastUnits === 0, "waiting does nothing");
                rejects(()=>loader.provide(99, new Uint8Array(4)), "provide: no such file");
                rejects(()=>loader.provide(todo, new Uint8Array(0)), "provide: empty");
                loader.provide(todo, await load(deps[todo].uri));
                rejects(()=>loader.provide(todo, new Uint8Array(4)), "provide once");
            }
            return waited;
        };
        const gltfBytes = await load("ext_bend.gltf");
        const ext = GLTF3D.beginMemory(gltfBytes, "ext_bend.gltf", undefined, { dependencies: true });
        assert(ext.dependencies.length === 0, "no dependencies before the parse");
        assert(await drive(ext, ["ext bend.bin"]) === 1, "one file waited for");
        const asset = ext.finish(), disk = GLTF3D.load(path+"ext_bend.gltf");
        assert(asset.names.join() === disk.names.join() && Object.keys(asset.clips).join() === Object.keys(disk.clips).join(), "external scene equals disk scene");
        free(asset); free(disk);
        const mesh = Model3D.beginMemory(await load("ext_textured.gltf"), "ext_textured.gltf", undefined, { dependencies: true });
        await drive(mesh, ["ext_textured.bin", "checker.png"]);
        const textured = mesh.finish(); assert(textured.vertexCount === Model3D.load(path+"ext_textured.gltf").vertexCount, "external mesh equals disk mesh");
        textured.dispose();
        const glb = GLTF3D.beginMemory(await load("memory_external_image.glb"), "img.glb", undefined, { dependencies: true });
        await drive(glb, ["checker.png"]); free(glb.finish());
        // without the option the strict memory rules still hold, and bad options are rejected
        const strict = GLTF3D.beginMemory(gltfBytes, "ext_bend.gltf");
        rejects(()=>{ while (!strict.step()); }, "strict loader refuses a .gltf"); strict.dispose();
        rejects(()=>GLTF3D.beginMemory(gltfBytes, "x.gltf", undefined, { dependencies: 1 }), "options.dependencies type");
        rejects(()=>GLTF3D.beginMemory(gltfBytes, "x.gltf", undefined, 5), "options type");
        rejects(()=>ext.provide(0, new Uint8Array(4)), "provide on a spent loader");
        const late = GLTF3D.beginMemory(gltfBytes, "late.gltf", undefined, { dependencies: true });
        rejects(()=>late.provide(0, new Uint8Array(4)), "provide before the parse"); late.step(); late.dispose();
        const text = String.fromCharCode(...gltfBytes).replace("ext%20bend.bin", "..%2Fx.bin");
        const bad = GLTF3D.beginMemory(Uint8Array.from(text, ch => ch.charCodeAt(0)), "bad.gltf", undefined, { dependencies: true });
        rejects(()=>{ while (!bad.step()); }, "uri leaving the folder"); bad.dispose();
        for (let i = 0; i < 3; i++) { const l = GLTF3D.beginMemory(gltfBytes, "gc.gltf", undefined, { dependencies: true }); while (l.phase !== "dependencies") l.step(); l.provide(0, await load("ext bend.bin")); }
    }
    // Zero-copy (borrow), time-sliced run() and per-phase statistics.
    {
        const bytes = new Uint8Array(await Thread.readFileAsync("models/arm.glb")), whole = GLTF3D.loadMemory(bytes, "arm.glb");
        const b = GLTF3D.beginMemory(bytes, "arm.glb", undefined, { borrow: true });
        assert(b.phase === "parse", "borrowed loader has no copy phase");
        let calls = 0; while (!b.run(0)) { calls++; assert(b.lastUnits === 1, "budget 0 runs one unit"); }
        const stats = b.phaseStats; assert(stats.parse.units === 1 && stats.nodes.units >= 3 && stats.nodes.maxMs <= stats.nodes.totalMs + 1e-6, "phase stats");
        const into = new Float64Array(3 * GLTF3D.Loader.PHASES.length); b.phaseStatsInto(into);
        for (const [name, st] of Object.entries(stats)) { const k = GLTF3D.Loader.PHASES.indexOf(name); assert(k >= 0 && into[3*k] === st.units && into[3*k+1] === st.totalMs && into[3*k+2] === st.maxMs, "phaseStatsInto matches phaseStats "+name); }
        rejects(()=>b.phaseStatsInto(new Float64Array(2)), "phaseStatsInto too short"); rejects(()=>b.phaseStatsInto(new Float32Array(40)), "phaseStatsInto type");
        assert(GLTF3D.Loader.PHASES[0] === "copy" && Model3D.Loader.PHASES.includes("build"), "PHASES");
        const a = b.finish(); assert(a.nodes.length === whole.nodes.length && a.names.join() === whole.names.join(), "borrowed scene equals one-shot");
        free(a);
        const t = GLTF3D.beginMemory(bytes, "arm.glb", undefined, { borrow: true }); let n = 0;
        while (!t.run(1000)) n++;
        assert(n === 0 && t.lastUnits >= 5, "a large budget finishes in one call"); free(t.finish());
        const unaligned = new Uint8Array(bytes.length + 1); unaligned.set(bytes, 1); const odd = unaligned.subarray(1);
        const u = GLTF3D.beginMemory(odd, "odd.glb", undefined, { borrow: true });
        assert(u.phase === "copy", "unaligned input falls back to the snapshot"); u.dispose();
        for (const bad of [-1, 1001, "x", NaN]) { const l = GLTF3D.beginMemory(bytes, "arm.glb", undefined, { borrow: true }); rejects(()=>l.run(bad), "run budget "+bad); l.dispose(); }
        rejects(()=>GLTF3D.beginMemory(bytes, "x.glb", undefined, { borrow: "yes" }), "borrow type");
        const m = Model3D.beginMemory(new Uint8Array(await Thread.readFileAsync("models/lit_cube.glb")), "cube.glb", undefined, { borrow: true });
        assert(m.phase === "parse", "borrowed mesh loader"); while (!m.run(0.5)); assert(m.phaseStats.build.units >= 1, "mesh phase stats"); m.finish().dispose();
        free(whole);
    }
    // Texture.loadMemory: PNG bytes without file I/O (the Assets3D job path), and the fast-upload switch.
    {
        const png = new Uint8Array(await Thread.readFileAsync(path+"checker.png")), disk = Model3D.Texture.load(path+"checker.png", 1, 3);
        const mem = Model3D.Texture.loadMemory(png, 1, 3);
        assert(mem.width === disk.width && mem.height === disk.height && mem.wrap === disk.wrap, "Texture.loadMemory equals load");
        const odd = new Uint8Array(png.length + 1); odd.set(png, 1);
        const sub = Model3D.Texture.loadMemory(odd.subarray(1), 0, 0); assert(sub.width === disk.width, "Texture.loadMemory honors subarrays");
        odd.fill(0); assert(sub.width === disk.width && mem.width === disk.width, "loadMemory copies the pixels");
        rejects(()=>Model3D.Texture.loadMemory(new Uint8Array(0)), "empty image"); rejects(()=>Model3D.Texture.loadMemory(new Uint8Array(16)), "not an image");
        rejects(()=>Model3D.Texture.loadMemory(png, 2), "filter"); rejects(()=>Model3D.Texture.loadMemory(png, 0, 7), "wrap"); rejects(()=>Model3D.Texture.loadMemory(null), "bytes");
        mem.dispose(); sub.dispose(); disk.dispose();
    }
    std.gc();
    console.log(`GLB memory JS test passed (${checks} checks)`);
}
main().catch(e=>{ console.log("[FAIL] "+e+"\n"+(e.stack||"")); });
