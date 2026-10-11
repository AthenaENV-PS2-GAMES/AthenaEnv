/**
 * 3D asset kinds for `Scene.Assets` manifests, reference counted like the
 * built-in kinds: shared by scenes, disposed when the last holder lets go.
 *
 * - `meshes`: `Model3D.Mesh`, `"ship.obj"` or `{ path, material }`
 *   (`baseColor` may be a plain array);
 * - `textures3d`: `Model3D.Texture` (PNG/JPEG/BMP files are read and decoded by a job, then
 *   adopted in O(1): a job-pool worker reads and decodes the file, so neither file I/O nor the decode
 *   (about 80 ms for 256x256 on the EE) runs inside update(); `asyncRead: false` selects `Texture.load`), `{ path, filter, wrap, upload }`
 *   (`upload: true` makes it resident in VRAM during the loading screen);
 * - `gltf`: `GLTF3D.Asset`, `{ path, material }` (needs GLTF3D); freeing it
 *   disposes its nodes and clips and detaches its root.
 *
 * GLB mesh/scene reads use Thread jobs by default, one read at a time
 * (16 MiB maximum). `gltf` scenes are then built in resumable units by
 * `GLTF3D.Loader` (parse, validation, one primitive or node, links, one skin
 * or animation) spread across update() calls; single GLB meshes use
 * `Model3D.Loader` (copy, parse, validate, build). Under a budget, update()
 * runs as many units as fit (the native loader times each unit; one unit can
 * overshoot). Loaders borrow the bytes read by the job (no copy). The `stepped`
 * option (`false`) builds a whole GLB per update() call instead, and
 * `Assets3D.setStepped()` changes the default; it does not affect the cache key.
 * `.gltf` files and GLBs that name relative external buffers/images are
 * resolved explicitly: the loader lists the files, Assets3D reads each one on a
 * Thread job (paths relative to the model, no ".." or absolute URIs, at most 64
 * files) and hands the bytes over. `{ asyncRead: false }` selects the existing
 * path loader. `{ maxBytes }` lowers the per-file job limit and is part of
 * cache identity. OBJ/MTL and standalone textures use path loaders. Without
 * `GLTF3D.beginMemory`/`Model3D.beginMemory` only `.glb` goes through jobs.
 * The budget is soft: one unit (a texture decode, a large primitive, a mesh,
 * an upload) can exceed it; stats().phases reports each phase's largest. Releasing the last
 * holder cancels queued/read jobs; another holder keeps the shared load alive.
 * Importing the module registers the kinds. Call update() without Loop.
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
    /** Soft main-thread budget in milliseconds (default 8); no strict bound. */
    const budgetMs: number;
    function setBudget(ms: number): void;
    /** Default of the per-asset `stepped` option (default true). */
    function setStepped(stepped: boolean): void;
    /** Polls reads without waiting and starts/constructs work. Returns operations run. */
    function update(): number;
    /** Read progress (bytes) is separate from construction progress (units,
     * only for resumable gltf loads; unitsTotal is 0 until validation counts them). */
    function pending(): { path: string; phase: 'queued' | 'reading' | 'dependencies' | 'construct'; bytesDone: number; bytesTotal: number;
        constructPhase: GLTF3D.LoaderPhase | Model3D.LoaderPhase | 'disposed' | null; unitsDone: number; unitsTotal: number }[];
    interface LoadMetrics {
        path: string; mode: 'job-memory' | 'job-memory-steps' | 'job-decode' | 'path'; bytes: number;
        /** External files read for a .gltf or a GLB with relative URIs, and their wall time. */
        dependencies?: number; dependencyWaitMs?: number;
        /** Elapsed wall time waiting for read/poll, including intervening frames. */
        readWaitMs: number;
        /** Parse + native/JS construction (also I/O/decode for path loaders). */
        constructMs: number;
        /** Resumable loads: units run (begin, steps and finish). */
        steps?: number;
        uploadMs: number;
    }
    /** CPU clocks include instrumentation; spans must finish before EE wrap (~14.5s).
     * Maxima are cumulative for this runtime. Cached texture upload requests
     * contribute to maxUploadMs, even when no new load runs. maxConstructionMs is
     * the largest single construction unit; phases lists each resumable unit kind
     * (count = units run, maxMs = the largest single unit; 'begin' creates the loader, 'finish' wraps the result in JS handles; no 'copy' phase for job reads). */
    function stats(): { queued: number; reading: number; completed: number; failed: number; cancelled: number;
        lastFrameMs: number; maxFrameMs: number; maxConstructionMs: number; maxUploadMs: number;
        lastLoad: LoadMetrics | null; budgetMs: number;
        phases: { [phase: string]: { count: number; totalMs: number; maxMs: number } } };
    /** Clears lastLoad, the frame/construction/upload maxima and phases; counters and the queue stay. */
    function resetStats(): void;
    const KINDS: string[];
}
