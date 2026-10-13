/** Static and captured texture shadow decals. Render opaque receivers first.
 * Textures use Model3D.Texture, independently of Image.
 * Position/scale accept xyz objects or arrays; rotation is a normalized xyzw
 * quaternion. Properties return copies; assigning TRS replaces setTransform. */
declare namespace Shadows {
    const SHADOW_BLEND_DARKEN: 0;
    const SHADOW_BLEND_ALPHA: 1;
    const SHADOW_BLEND_ADD: 2;
    type Vec3 = {x: number; y: number; z: number} | [number, number, number] | Float32Array;
    type Quaternion = {x: number; y: number; z: number; w: number} | [number, number, number, number] | Float32Array;
    /** GPU silhouette texture owner. Keep this alive while a projector uses
     * its texture; dispose the projector before the map. */
    class ShadowMap {
        constructor(width?: number, height?: number, filter?: 0 | 1);
        readonly texture: Model3D.Texture;
        /** Independent light camera; match its aspect to width/height. The
         * callback receives that camera for Scene3D/animated caster draws.
         * Materials and camera objects are never modified by capture. */
        capture(camera: Camera3D.Camera, caster: Model3D.Instance): undefined;
        capture<R>(camera: Camera3D.Camera, caster: (camera: Camera3D.Camera) => R): R;
        dispose(): void;
    }
    class Projector {
        /** Omitted texture creates a soft circular blob owned by the projector.
         * Supplied textures are borrowed: keep the handle live until dispose(). */
        constructor(texture?: Model3D.Texture);
        position: Vec3;
        rotation: Quaternion;
        scale: Vec3;
        setTransform(matrix: Matrix4): this;
        setSize(width: number, height: number): this;
        /** Grid dimensions 2..64; default 2x2, on the local XZ plane. */
        setGrid(x: number, z: number): this;
        /** Direction rays travel, normalized; default (0,-1,0). */
        setLightDir(x: number, y: number, z: number): this;
        /** Offset opposite lightDir, applied to all nodes; default .01. */
        setBias(bias: number): this;
        setLightOffset(distance: number): this;
        /** Minimum hit normal Y in [-1,1]; -1 disables the filter. */
        setSlopeLimit(cosine: number): this;
        /** Linear RGBA in [0,1]; default black with .75 opacity. */
        setColor(r: number, g: number, b: number, a: number): this;
        setBlend(mode: 0 | 1 | 2): this;
        setUVRect(u0: number, v0: number, u1: number, v1: number): this;
        /** Casts a segment centered on each transformed node along lightDir.
         * Misses/steep nodes omit incident triangles. The world is borrowed.
         * Queries run every render; geometry rebuilds only when it changes. */
        enableRaycast(world: Collision3D.World | null, rayLength?: number, enable?: boolean): this;
        /** Requires a camera and Screen zbuffering. Depth tests stay enabled,
         * depth writes disabled; restores GS state. Avoid calling in group().
         * Returns undefined when no triangle survives terrain projection. */
        render(camera: Camera3D.Camera, stats?: object | null): Render3D.Stats | undefined;
        dispose(): void;
    }
}
