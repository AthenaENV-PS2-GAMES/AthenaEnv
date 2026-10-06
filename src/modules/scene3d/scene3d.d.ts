/** Native transform hierarchy. A Node is the scene-graph instance of a mesh;
 * world = parent world * local TRS. Setters only mark dirty flags: call
 * scene.update() (or attachLoop()) before draw and world queries, which throw
 * while the scene is stale instead of returning outdated data. */
declare namespace Scene3D {
    /** Levels from the root, root included. Deeper hierarchies are rejected. */
    const MAX_DEPTH: number;
    interface UpdateStats { visitedNodes: number; worldUpdates: number; boundsUpdates: number; }
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
        /** Hidden nodes and descendants are not drawn nor included in bounds. */
        visible: boolean;
        /** Reparents child, keeping its local transform; the parent retains it.
         * Throws RangeError for cycles, scene roots and MAX_DEPTH overflow. */
        add(child: Node): this;
        /** Removes this node from its parent; harmless without one. */
        detach(): this;
        /** New handle for the parent, or null. Handles are not identical objects. */
        getParent(): Node | null;
        readonly childCount: number;
        /** New handle for the child at index. */
        getChild(index: number): Node;
        /** Always current. Owned snapshot, or fills and returns out. */
        getLocalTransform(out?: Matrix4): Matrix4;
        /** Transform of the last update. Throws while stale or outside a scene. */
        getWorldTransform(out?: Matrix4): Matrix4;
        /** World AABB of visible meshes in the subtree, or null when empty.
         * Throws while stale or outside a scene. */
        getWorldBounds(): Bounds | null;
        /** Drops this handle; parents and other handles keep the node alive. */
        dispose(): void;
    }
    class Scene {
        constructor();
        /** New handle for the root node owned by the scene. */
        readonly root: Node;
        /** True when a node changed after the last update. */
        readonly stale: boolean;
        /** True while a Loop POST_UPDATE system updates this scene. */
        readonly attached: boolean;
        /** Recomputes dirty world transforms and subtree bounds. */
        update(): UpdateStats;
        /** Culls subtrees, queues meshes and draws them through Render3D.
         * Never updates the scene; throws while stale. Lights are borrowed. */
        draw(camera: Camera3D.Camera, cullMode?: Render3D.CullMode, lights?: Lights.Set): DrawStats;
        /** Updates natively in Loop POST_UPDATE. Lower priority runs first. */
        attachLoop(priority?: number): this;
        detachLoop(): this;
        /** Also detaches from the Loop; existing node handles stay valid. */
        dispose(): void;
    }
}
