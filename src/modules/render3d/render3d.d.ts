/** Native opaque unlit/diffuse and textured triangles with homogeneous clipping in C for
 * crossing objects; VU1 transforms and lights fully contained objects.
 * Draw never advances animation/physics. Enable Screen zbuffering. */
declare namespace Render3D {
    const CULL_NONE: 0; const CULL_BACK: 1; const CULL_FRONT: -1;
    type CullMode = 0 | 1 | -1;
    interface Stats {
        submittedObjects: number; culledObjects: number;
        /** Accepted objects drawn, even when clipping rejects all their triangles. */
        drawPasses: number;
        /** GS/VU1 passes emitted. Batch and Scene3D draws share one pass among
         * consecutive objects with the same camera, program and texture. */
        pipelinePasses: number;
        /** Triangle list sent to VU1 after native clipping; not rasterized count. */
        triangles: number; vuBatches: number;
        /** Source triangles of objects retained by AABB culling. */
        sourceTriangles: number;
        /** Source triangles partially clipped and producing visible polygons. */
        clippedTriangles: number;
        /** Source triangles rejected by precise clipping. */
        rejectedTriangles: number;
        /** Copied position/color/normal/UV DMA payload, including chunk padding.
         * Excludes tags, constants, texture/program uploads, GS state and 2D draws. */
        geometryBytes: number;
        /** Objects crossing the screen edges drawn by VU1 without clipping,
         * inside the GS guard band (the scissor trims them). */
        guardBandObjects: number;
        /** Objects crossing the near plane clipped on VU1 (inside the guard
         * band otherwise); their triangles count before clipping. */
        nearClipObjects: number;
        /** Meshes with morph targets blended on VU1. */
        vuMorphObjects: number;
    }
    /** Lights are borrowed for this call. Omitted lights mean black ambient and
     * no directional lights. UNLIT materials ignore lights. Singular DIFFUSE
     * normal transforms throw; drawing does not update lights or transforms.
     * Pass `stats` to reuse an object every frame: its fields are assigned and
     * it is returned, instead of allocating a new Stats per call. */
    function draw<T extends object = Stats>(instance: Model3D.Instance, camera: Camera3D.Camera, cullMode?: CullMode,
        lights?: Lights.Set, stats?: T): T & Stats;
    class Batch {
        constructor();
        readonly size: number;
        /** Retains the native instance, independently of its JS handle. */
        add(instance: Model3D.Instance): this;
        clear(): this;
        /** Optional `stats` is reused and returned, as in Render3D.draw(). */
        draw<T extends object = Stats>(camera: Camera3D.Camera, cullMode?: CullMode, lights?: Lights.Set,
            stats?: T): T & Stats;
        dispose(): void;
    }
}
