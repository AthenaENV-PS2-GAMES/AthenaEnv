/** Native opaque unlit/diffuse and textured triangles with homogeneous clipping in C for
 * crossing objects; VU1 transforms and lights fully contained objects.
 * Draw never advances animation/physics. Enable Screen zbuffering. */
declare namespace Render3D {
    const CULL_NONE: 0; const CULL_BACK: 1; const CULL_FRONT: -1;
    type CullMode = 0 | 1 | -1;
    interface Stats {
        submittedObjects: number; culledObjects: number; drawPasses: number;
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
    }
    /** Lights are borrowed for this call. Omitted lights mean black ambient and
     * no directional lights. UNLIT materials ignore lights. Singular DIFFUSE
     * normal transforms throw; drawing does not update lights or transforms. */
    function draw(instance: Model3D.Instance, camera: Camera3D.Camera, cullMode?: CullMode, lights?: Lights.Set): Stats;
    class Batch {
        constructor();
        readonly size: number;
        /** Retains the native instance, independently of its JS handle. */
        add(instance: Model3D.Instance): this;
        clear(): this;
        draw(camera: Camera3D.Camera, cullMode?: CullMode, lights?: Lights.Set): Stats;
        dispose(): void;
    }
}
