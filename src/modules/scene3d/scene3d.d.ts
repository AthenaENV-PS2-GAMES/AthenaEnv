/** Native transform hierarchy. A Node is the scene-graph instance of a mesh;
 * world = parent world * local TRS. Setters only mark dirty flags: call
 * scene.update() (or attachLoop()) before draw and world queries, which throw
 * while the scene is stale instead of returning outdated data. */
declare namespace Scene3D {
    /** Releases reusable CPU skinning/morph buffers after draw returns, for
     * a level transition or memory pressure. They grow again when needed. */
    function trimScratch(): void;
    /** Levels from the root, root included. Deeper hierarchies are rejected. */
    const MAX_DEPTH: number;
    /** Bulk setters, one call per frame instead of one per node: values holds
     * x, y, z for nodes[i] at values[3i..3i+2] (it may be longer). Every
     * value is checked finite before any node changes. Returns the count. */
    function setPositions(nodes: Node[], values: Float32Array): number;
    /** Radians, composed Rz * Ry * Rx as Node.setRotationEuler(). */
    function setRotationsEuler(nodes: Node[], values: Float32Array): number;
    /** 2D physics on 3D nodes: x, y and an angle about Z (radians) per node,
     * the layout of Box2D's `world.readTransforms()`. Each node keeps its z.
     * ```js
     * world.readTransforms(bodies, transforms);
     * Scene3D.setTransforms2D(crates, transforms);
     * ``` */
    function setTransforms2D(nodes: Node[], values: Float32Array): number;
    interface UpdateStats { visitedNodes: number; worldUpdates: number; boundsUpdates: number; }
    /** Nearest raycast hit: node, distance along the ray, world point
     * (x, y, z), unit normal facing the ray (nx, ny, nz) and source triangle
     * index (-1 for an AABB hit). */
    interface RaycastHit {
        node: Node; distance: number; x: number; y: number; z: number;
        nx: number; ny: number; nz: number; triangle: number;
    }
    interface RaycastOptions {
        /** Test triangles (default true) or only the meshes' world AABBs. */
        precise?: boolean;
    }
    interface DrawStats extends Render3D.Stats {
        /** Subtrees rejected by their world bounds; their meshes count as culled. */
        culledSubtrees: number;
        /** Meshes sent to Render3D, sorted by pipeline in traversal order. */
        queuedObjects: number;
    }
    interface Bounds { min: [number, number, number]; max: [number, number, number]; }
    class Node {
        /** Retains the optional mesh natively, independently of its handle. */
        constructor(mesh?: Model3D.Mesh);
        /** Replaces the retained mesh; null removes it. */
        setMesh(mesh: Model3D.Mesh | null): this;
        readonly hasMesh: boolean;
        setPosition(x: number, y: number, z: number): this;
        setScale(x: number, y: number, z: number): this;
        /** Radians, XYZ local rotations composed Rz * Ry * Rx. */
        setRotationEuler(x: number, y: number, z: number): this;
        setRotationQuaternion(x: number, y: number, z: number, w: number): this;
        /** Native motion, integrated by scene.advance(dt) / attachLoop() without
         * a JS call per frame. Units per second in the parent space. */
        setVelocity(x: number, y: number, z: number): this;
        /** Angular velocity in rad/s about the local axes: direction is the
         * axis, length the speed. (0, 0, 0) stops the spin. */
        setSpin(x: number, y: number, z: number): this;
        /** Hidden nodes and descendants are not drawn nor included in bounds. */
        visible: boolean;
        /** Reparents child, keeping its local transform; the parent retains it.
         * Throws RangeError for cycles, scene roots and MAX_DEPTH overflow. */
        add(child: Node): this;
        /** Removes this node from its parent; harmless without one. */
        detach(): this;
        /** The same live JS object for this native parent, or null. Wrapper
         * identity (including subclass/properties) is preserved while live. */
        getParent(): Node | null;
        readonly childCount: number;
        /** The same live JS object for the child at index. */
        getChild(index: number): Node;
        /** Always current. Owned snapshot, or fills and returns out. */
        getLocalTransform(out?: Matrix4): Matrix4;
        /** Transform of the last update. Throws while stale or outside a scene. */
        getWorldTransform(out?: Matrix4): Matrix4;
        /** World AABB of visible meshes in the subtree, or null when empty.
         * Throws while stale or outside a scene. */
        getWorldBounds(): Bounds | null;
        /** Morph target weights (up to 8; missing ones become 0). A mesh with
         * morph targets is blended in C at draw: base + sum(weight * delta). */
        setWeights(weights: ArrayLike<number>): this;
        /** One weight per morph target of the node's mesh (empty without). */
        getWeights(): number[];
        /** Invalidates this shared JS wrapper (all aliases). Parents/scenes
         * retain the native node; later access can create another wrapper.
         * The wrapper cache is weak and does not keep JS objects alive. */
        dispose(): void;
    }
    class Scene {
        constructor();
        /** The same live JS object for the root node owned by the scene. */
        readonly root: Node;
        /** True when a node changed after the last update. */
        readonly stale: boolean;
        /** True while a Loop POST_UPDATE system updates this scene. */
        readonly attached: boolean;
        /** Integrates node motion (setVelocity/setSpin) for dt seconds and
         * returns the moved node count. attachLoop() does it every frame with
         * the Loop dt, before update(). */
        advance(dt: number): number;
        /** Recomputes dirty world transforms and subtree bounds. Optional
         * `stats` is reused and returned instead of allocating a new object;
         * null skips it and returns undefined. */
        update<T extends object = UpdateStats>(stats?: T): T & UpdateStats;
        update(stats: null): undefined;
        /** Culls subtrees, queues meshes and draws them through Render3D.
         * Never updates the scene; throws while stale. Lights are borrowed.
         * `stats` as in Render3D.draw(): reused, or null to return undefined;
         * the render totals feed Render3D.frameStats() either way. */
        draw<T extends object = DrawStats>(camera: Camera3D.Camera, cullMode?: Render3D.CullMode, lights?: Lights.Set,
            stats?: T): T & DrawStats;
        draw(camera: Camera3D.Camera, cullMode: Render3D.CullMode | undefined, lights: Lights.Set | undefined,
            stats: null): undefined;
        /** Nearest visible mesh hit by the ray (e.g. Camera3D.Camera.screenToRay())
         * within maxDistance (default unlimited), or null. Uses the transforms of
         * the last update(); throws while stale. Skinned meshes are skipped and
         * morphed meshes are tested in their base pose. Optional `out` is reused. */
        raycast<T extends object = RaycastHit>(ray: Camera3D.Ray, maxDistance?: number, options?: RaycastOptions,
            out?: T): (T & RaycastHit) | null;
        /** Nodes whose visible, unskinned mesh world AABB overlaps the box, in
         * traversal order. Optional `out` array is cleared, refilled and returned. */
        queryBox(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number,
            out?: Node[]): Node[];
        /** Updates natively in Loop POST_UPDATE. Lower priority runs first. */
        attachLoop(priority?: number): this;
        detachLoop(): this;
        /** Also detaches from the Loop; existing node handles stay valid. */
        dispose(): void;
    }
}
