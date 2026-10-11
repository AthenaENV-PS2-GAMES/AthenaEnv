/** Independent linear RGB lights. World directions point toward the source.
 * Point lights (4 slots) light per vertex and fade with the distance as
 * (1 - d^2 / range^2)^2, reaching 0 at range. */
declare namespace Lights {
    const MAX_DIRECTIONAL: 4;
    const MAX_POINT: 4;
    class Set {
        /** Starts with black ambient and all four directional slots disabled. */
        constructor();
        /** Changes only when effective state changes; invalid setters are atomic. */
        readonly revision: number;
        setAmbient(r: number, g: number, b: number): this;
        /** Slots 0..3; nonzero direction normalized in native code. RGB in [0,1]. */
        setDirectional(slot: number, x: number, y: number, z: number, r: number, g: number, b: number): this;
        disable(slot: number): this;
        /** Slots 0..3: a world position, RGB in [0,1] and a range > 0. */
        setPoint(slot: number, x: number, y: number, z: number, r: number, g: number, b: number, range: number): this;
        disablePoint(slot: number): this;
        /** Distance fog, applied by the GS to everything drawn with this set:
         * full colour up to start, the fog colour from end on (view depth).
         * 0 <= start < end, RGB in [0,1]. Ignored with 32-bit Z buffers. */
        setFog(start: number, end: number, r: number, g: number, b: number): this;
        disableFog(): this;
        clear(): this;
        dispose(): void;
    }
}
