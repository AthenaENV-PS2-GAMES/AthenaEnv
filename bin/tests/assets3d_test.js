/*
 * Assets3D module: the meshes, textures3d and gltf kinds in Scene.Assets,
 * reference counting with disposal, budgeted loading between frames and
 * errors. Host test (tests/js/run.sh): the files come from tests/host/3d.
 */
import * as Assets3D from "Assets3D";
import * as Model3D from "Model3D";
import * as GLTF3D from "GLTF3D";
import * as Scene3D from "Scene3D"; // GLTF3D builds Scene3D nodes (the runtime bootstrap imports it)
void Scene3D;
import { Assets } from "Scene";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
globalThis.Model3D = globalThis.Model3D || Model3D;
globalThis.GLTF3D = globalThis.GLTF3D || GLTF3D;
const ROOT = typeof globalThis.__nativeDraws === "function" ? "../tests/host/3d" : "3d";
const pump = async (n = 10) => { for (let i = 0; i < n; i++) { Assets3D.update(); Assets.update(0); await null; } };

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
    await pump();
    await group.ready;
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
        await pump(); await gl.ready;
        check("gltf asset", gl.assets.gltf.tri.root && Array.isArray(gl.assets.gltf.tri.nodes));
        gl.release();
    } else {
        const gl = Assets.acquire({ gltf: { tri: "triangle.gltf" } }, ROOT);
        await pump();
        let e2 = null; try { await gl.ready; } catch (e) { e2 = e; }
        check("gltf without GLTF3D explains", e2 !== null && /GLTF3D/.test(String(e2.message || e2)));
        gl.release();
    }
    Assets3D.setBudget(8);
}
main().catch(e => { failed++; console.log("[FAIL] " + e + "\n" + (e.stack || "")); }).then(() => {
    console.log(`Result: ${passed} passed, ${failed} failed`);
    if (!failed) console.log("Assets3D module test passed");
});
