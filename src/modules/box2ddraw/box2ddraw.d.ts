/**
 * Debug drawing of a Box2D world: shape outlines, joints, bounds, contacts
 * and centers of mass, in Box2D's colors (static bodies green, awake
 * dynamic bodies pink, sleeping ones gray). Lines are batched into one GS
 * packet stream per call and what lies outside the screen is skipped.
 *
 * Example:
 * ```js
 * while (true) {
 *     world.step(1 / 60, 4);
 *     Screen.clear(BLACK);
 *     Box2DDraw.draw(world, { scale: 32, offsetX: 320, offsetY: 400 });
 *     Screen.flip();
 * }
 * ```
 */
declare namespace Box2DDraw {
    interface Options {
        /** Pixels per meter, 0.001 to 1e6. Default 32. */
        scale?: number;
        /** Screen position of the world origin. Default: the screen center. */
        offsetX?: number;
        offsetY?: number;
        /** World y up, screen y down. Default true. */
        flipY?: boolean;
        /** Translucent shape interiors (needs alpha blending). Default false. */
        fill?: boolean;
        /** Default true. */
        shapes?: boolean;
        /** Default true. */
        joints?: boolean;
        /** Joint limits, springs and frames. Default false. */
        jointExtras?: boolean;
        /** Shape bounding boxes. Default false. */
        bounds?: boolean;
        /** Contact points and normals. Default false. */
        contacts?: boolean;
        /** Centers of mass. Default false. */
        mass?: boolean;
    }

    /** Draws the world. Call between Screen.clear() and Screen.flip(). */
    function draw(world: Box2D.World, options?: Options): void;
}
