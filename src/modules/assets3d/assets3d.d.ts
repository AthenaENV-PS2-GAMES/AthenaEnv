/**
 * 3D asset kinds for `Scene.Assets` manifests, reference counted like the
 * built-in kinds: shared by scenes, disposed when the last holder lets go.
 *
 * - `meshes`: `Model3D.Mesh`, `"ship.obj"` or `{ path, material }`
 *   (`baseColor` may be a plain array);
 * - `textures3d`: `Model3D.Texture`, `{ path, filter, wrap, upload }`
 *   (`upload: true` makes it resident in VRAM during the loading screen);
 * - `gltf`: `GLTF3D.Asset`, `{ path, material }` (needs GLTF3D); freeing it
 *   disposes its nodes and clips and detaches its root.
 *
 * Model3D and GLTF3D parse synchronously: loads are queued and run between
 * frames for up to `budgetMs` (at least one per frame), so the loading
 * screen keeps drawing. Importing the module registers the kinds.
 *
 * Not in the default build: `node tools/modules.js configure --modules=assets3d,...`
 *
 * Example:
 * ```js
 * class Level extends Scene {
 *     static root = "assets/level1";
 *     static assets = {
 *         meshes: { ship: { path: "ship.obj", material: { shading: Model3D.DIFFUSE } } },
 *         textures3d: { rock: { path: "rock.png", filter: 1, wrap: 3, upload: true } },
 *         gltf: { hero: "hero.glb" },
 *     };
 *     enter(assets) { this.scene.root.add(assets.gltf.hero.root); }
 * }
 * ```
 */
declare namespace Assets3D {
    /** Milliseconds of loading per frame (default 8). */
    const budgetMs: number;
    function setBudget(ms: number): void;
    /** Runs queued loads (the Loop system does it); returns how many ran. */
    function update(): number;
    function stats(): { queued: number; completed: number; failed: number; lastFrameMs: number; budgetMs: number };
    const KINDS: string[];
}
