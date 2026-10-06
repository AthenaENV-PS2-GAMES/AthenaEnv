/** Independent linear RGB lights. World directions point toward the source. */
declare namespace Lights {
    const MAX_DIRECTIONAL: 4;
    class Set {
        /** Starts with black ambient and all four directional slots disabled. */
        constructor();
        /** Changes only when effective state changes; invalid setters are atomic. */
        readonly revision: number;
        setAmbient(r: number, g: number, b: number): this;
        /** Slots 0..3; nonzero direction normalized in native code. RGB in [0,1]. */
        setDirectional(slot: number, x: number, y: number, z: number, r: number, g: number, b: number): this;
        disable(slot: number): this;
        clear(): this;
        dispose(): void;
    }
}
