/** glTF 2.0 / GLB scenes with their node hierarchy and animations.
 * Model3D.load() keeps reading single meshes; GLTF3D.load() reads whole
 * scenes: nodes become Scene3D nodes (TRS, or a matrix decomposed into TRS),
 * every triangle primitive a Model3D mesh (extra primitives of a mesh become
 * child nodes), and every animation an Animation3D clip whose targets are
 * node indices. Skins deform their meshes every draw (in C) from the joint
 * nodes, which the clips animate. Morph targets and CUBICSPLINE tracks are
 * supported within the limits of Model3D/Animation3D.
 * ```js
 * const hero = GLTF3D.load("models/hero.glb");
 * scene.root.add(hero.root);
 * const walk = new Animation3D.Player(hero.clips.walk, hero.nodes);
 * walk.loop = true; walk.play();
 * ``` */
declare namespace GLTF3D {
    interface Asset {
        /** Group holding the default scene's root nodes. */
        root: Scene3D.Node;
        /** Every node in file order: the clips' target indices. */
        nodes: Scene3D.Node[];
        /** Node names, "" when unnamed. */
        names: string[];
        /** Clips by animation name (the index for unnamed ones). */
        clips: { [name: string]: Animation3D.Clip };
    }
    /** material, as in Model3D.load(), replaces every primitive's material. */
    function load(path: string, material?: Model3D.Material): Asset;
    /** Self-contained GLB, at most 16 MiB. Borrows ArrayBuffer/Uint8Array
     * bytes only for this synchronous main-thread call; all scene data and
     * decoded pixels are copied. Subarrays may be unaligned (temporary copy).
     * origin is a nonempty diagnostic name. URI buffers/external images are
     * rejected before construction; no file I/O or GS upload. */
    function loadMemory(bytes: ArrayBuffer | Uint8Array, origin: string, material?: Model3D.Material): Asset;

    type LoaderPhase = 'copy' | 'parse' | 'dependencies' | 'validate' | 'nodes' | 'links' | 'skins' | 'clips' | 'done';
    /** Resumable GLB load: each step() runs one bounded unit on the main
     * thread. The first steps ('copy', 256 KiB each) snapshot the bytes into
     * memory the loader owns; the source is pinned until the phase leaves
     * 'copy', so do not modify it before that. Later units are atomic: the
     * parse, the validation, one primitive (including decoding its embedded
     * textures) or meshless node, the hierarchy links, one skin or one
     * animation. Dispose it to abandon the load; unreferenced loaders are
     * freed by GC. */
    class Loader {
        private constructor();
        /** Phase names by native phase index (the order of phaseStatsInto). */
        static readonly PHASES: readonly string[];
        /** Runs up to `units` (1..64, default 1) consecutive units of the same
         * phase. false while work remains, true once finished. Throws the load
         * error, and keeps throwing it; then dispose(). */
        step(units?: number): boolean;
        /** Runs at least one unit, then more until `budgetMs` (0..1000, default 0 =
         * one unit) has elapsed: a single call for a time slice, timed by the
         * native loader. Same results as step(); false while work remains, true
         * once finished; lastUnits is the number of units that ran. */
        run(budgetMs?: number): boolean;
        /** Units, total time and largest single unit per phase so far. */
        /** Same numbers as `phaseStats`, written into a Float64Array (units, totalMs,
         * maxMs at index 3 * phase index; at least 3 * PHASES.length elements): no allocation. */
        phaseStatsInto(out: Float64Array): void;
        readonly phaseStats: { [phase: string]: { units: number; totalMs: number; maxMs: number } };
        /** The Asset, after step() returned true. Spends the loader. */
        finish(): Asset;
        /** Frees whatever was not delivered; idempotent. */
        dispose(): void;
        /** The next unit's phase ('disposed' once finished or disposed). */
        readonly phase: LoaderPhase | 'disposed';
        /** The phase the last step() ran. */
        readonly lastPhase: LoaderPhase | 'disposed';
        /** Native steps the last step() call ran (at most `units`). */
        readonly lastUnits: number;
        /** Files named by relative URI (external buffers and images), listed after
         * the parse step when started with `{ dependencies: true }`; else empty.
         * Read each one yourself (the loader does no I/O) and give it to provide(). */
        readonly dependencies: { uri: string; image: boolean; provided: boolean }[];
        /** Hands over the bytes of dependencies[index] (ArrayBuffer/Uint8Array,
         * 1 byte to 16 MiB, once). They are used, not copied: do not modify the
         * buffer until finish() or dispose(). While files are missing step()
         * returns false and does nothing (phase 'dependencies'). Absolute, scheme,
         * drive and ".." URIs are rejected, at most 64 files. */
        provide(index: number, bytes: ArrayBuffer | Uint8Array): void;
        readonly unitsDone: number;
        /** 0 until validation has counted the units. */
        readonly unitsTotal: number;
    }
    /** Starts a resumable load of a self-contained GLB (see loadMemory for the
     * input rules, checked while stepping). Snapshots bytes <= 16 MiB while
     * the loader is in its 'copy' phase. *
     * `options.borrow: true` uses the bytes in place (no 'copy' phase and no snapshot
     * RAM) when they are 4-byte aligned, otherwise the snapshot is made as usual; the
     * buffer is pinned and must not be modified until finish() or dispose(). */
    function beginMemory(bytes: ArrayBuffer | Uint8Array, origin: string, material?: Model3D.Material, options?: { dependencies?: boolean; borrow?: boolean }): Loader;
}
