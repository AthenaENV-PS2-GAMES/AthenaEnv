/**
 * A native 2D sprite queue with recorded layers, atlases and bitmap fonts.
 *
 * Sprites of one image gather in a buffer and go to the GS in one packet;
 * recording a scene's static parts into a {@link SpriteBatch.Layer} lets it
 * draw the same list again every frame. On the EE, building sprite lists in
 * JavaScript costs ~100 us per sprite (soft-float doubles, option objects),
 * so the per-sprite work (positions, flips, tints, glyph layout) lives in C:
 * `Atlas.draw` and `BitmapFont.draw` cost a few microseconds.
 *
 * Everything drawn goes through one queue, so draw order is the call order,
 * and it is sent when the image changes, when a rect/quad is drawn, on
 * `flush()` or when a layer draws. Call `flush()` at the end of a scene's
 * draw. Sprites outside the 2D camera's viewport are culled like in
 * `Image.drawList`.
 *
 * Not in the default build: `node tools/modules.js configure --modules=spritebatch,...`
 *
 * Example:
 * ```js
 * const atlas = new SpriteBatch.Atlas(image, info, 640, 448, 0.5);
 * const font = new SpriteBatch.BitmapFont(fontImage, fontInfo, 640, 448, 0.5, { color: [34, 17, 34] });
 *
 * const frame = SpriteBatch.capture(() => {      // once
 *     atlas.drawNinePatch("panel9", 100, 60, 400, 300, 24);
 *     font.drawShadow("Name", 140, 100);
 * });
 * Loop.run({ draw() { frame.draw(); SpriteBatch.flush(); } });
 * ```
 */
declare namespace SpriteBatch {
    /** A recorded list of draws (sprites of any number of images, rects, quads) that can be drawn again. */
    interface Layer {
        /** Sends the recorded draws in order. Sprites of an image that was freed are skipped. */
        draw(): void;
        /**
         * Changes the tint of the sprite recorded after `mark(name)`, in place:
         * microseconds instead of recording again. r, g, b are 0..255 (128 is neutral on
         * the GS). Returns false when no sprite has that mark.
         */
        tint(name: string, r: number, g: number, b: number): boolean;
        /** The sprites as `Image.drawList` records, one entry per run of the same image (tests, tools). */
        dump(): { image: Image; count: number; data: ArrayBuffer }[];
        /** Releases the layer's memory and its hold on the images. */
        free(): void;
    }

    /** Queues one sprite: (x, y, w, h) in pixels, (u1, v1)-(u2, v2) in texels (u1 > u2 mirrors); r, g, b, a 0..255, 128 neutral. */
    function sprite(image: Image, x: number, y: number, w: number, h: number,
        u1: number, v1: number, u2: number, v2: number, r: number, g: number, b: number, a: number): void;
    /** Sends the queued sprites. */
    function flush(): void;
    /** A flat rectangle, in order with the sprites. Under a camera sizes are world units; without one, sizes under 1 pixel are skipped. */
    function rect(x: number, y: number, w: number, h: number, color: number): void;
    /** A gouraud quad, with the argument order of `Draw.quadGouraud`. */
    function quad(x1: number, y1: number, c1: number, x2: number, y2: number, c2: number,
        x3: number, y3: number, c3: number, x4: number, y4: number, c4: number): void;
    /**
     * Runs `fn` and records everything drawn into a Layer instead of sending it. Layers do not nest and
     * keep up to 16 images alive. Throws what `fn` throws.
     */
    function capture(fn: () => void): Layer;
    function capturing(): boolean;
    /** `[sprites, batches]` sent to the GS since the last call (one batch = one packet); reading resets them. For profiling. */
    function stats(): [number, number];
    /** While recording, names the next sprite so `Layer.tint(name, ...)` can find it (up to 16 names). */
    function mark(name: string): void;
    /** Cancels a mark that was not used. */
    function unmark(): void;

    type RGB = readonly [number, number, number];

    /**
     * Named sprites of a texture atlas. Positions are in the original UI's pixels ("native"): `ratio` converts to
     * screen pixels. `info.sprites[name]` = `{ uv: [u1, v1, u2, v2], w, h, offset: [x, y], source_size: [w, h],
     * draw_scale, native_scale? }` where w/h are the trimmed size and offset where it sits in the original.
     */
    class Atlas {
        constructor(image: Image, info: { sprites: Record<string, object> }, screenWidth: number, screenHeight: number, ratio: number);
        readonly image: Image;
        readonly sprites: Record<string, any>;
        /** Index of a sprite for the faster `draw(index, ...)` form, or -1. */
        id(name: string): number;
        /** Original size in native pixels. */
        size(name: string): [number, number];
        /** Draws at native (x, y) of the untrimmed sprite; `mul` scales around its center, rgb tints (0..255), flip mirrors. Offscreen draws are skipped. */
        draw(sprite: string | number, x: number, y: number, mul?: number, alpha?: number, rgb?: RGB | null, flip?: boolean): void;
        /** Stretches the whole sprite over a native rectangle; edges snap to screen pixels so boxes made of pieces have no gaps. */
        drawRect(sprite: string | number, x: number, y: number, width: number, height: number, alpha?: number, rgb?: RGB | null): void;
        /** A 3x3 box from one sprite (kept untrimmed): corners keep their size (`corner` in native pixels), edges and center stretch. */
        drawNinePatch(sprite: string | number, x: number, y: number, width: number, height: number, corner: number, alpha?: number, rgb?: RGB | null): void;
    }

    /**
     * A bitmap font in the metrics of XNA's SpriteFont. `info.glyphs[char] = [x, y, w, h, cropX, cropY, left, advance, right]`
     * (texture rectangle, drawing offset and the three kerning values), `info.default` the fallback character,
     * `info.spacing` and `info.line_spacing`. `text` gives the default color, the shadow color and its offset.
     * Layouts are cached per string, so drawing the same text again only places the glyphs.
     */
    class BitmapFont {
        constructor(image: Image, info: object, screenWidth: number, screenHeight: number, ratio: number,
            text: { color?: RGB; shadow?: RGB; shadowOffset?: number });
        readonly image: Image;
        /** Width of the widest line, native pixels. */
        measure(text: string): number;
        height(text: string): number;
        draw(text: string, x: number, y: number, rgb?: RGB | null, alpha?: number): void;
        /** The shadow color 1 offset down and right, then the text. */
        drawShadow(text: string, x: number, y: number, rgb?: RGB | null, alpha?: number): void;
        drawCentered(text: string, centerX: number, y: number, rgb?: RGB | null, alpha?: number, shadow?: boolean): void;
    }
}
