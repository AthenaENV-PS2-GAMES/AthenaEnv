/**
 * Image loading, CPU pixel access and textured 2D drawing.
 *
 * `Image` accepts paths understood by the active PS2 filesystem driver,
 * including paths relative to the boot directory. A newly loaded image is
 * CPU-resident; call `lock()` when it must remain resident in VRAM.
 *
 * Pixel buffers use the image's current `bpp` and dimensions. For 32-bit
 * images, `pixels` contains four bytes per pixel. Palette data is used only
 * by indexed 4-bit and 8-bit formats.
 *
 * @example
 * ```js
 * const logo = new Image('my_image.png');
 * if (!logo.ready()) throw new Error('image load failed');
 * logo.color = Color.new(255, 255, 255, 255);
 * logo.lock();
 * logo.draw(100, 80);
 * Screen.flip();
 * ```
 */
declare module "Image" {
    /** Optional destination, source-rectangle and tint overrides for `draw()`. */
    type ImageDrawOptions = {
        /** Destination width in pixels; defaults to `width`. */
        width?: number;
        /** Destination height in pixels; defaults to `height`. */
        height?: number;
        /** Source rectangle's left coordinate in texture pixels. */
        startx?: number;
        /** Source rectangle's top coordinate in texture pixels. */
        starty?: number;
        /** Source rectangle's right coordinate in texture pixels. */
        endx?: number;
        /** Source rectangle's bottom coordinate in texture pixels. */
        endy?: number;
        /** Rotation angle in radians. */
        angle?: number;
        /** Packed RGBA tint, normally created with `Color.new()`. */
        color?: number;
    };

    class Image {
        /** Loads an image from `path`, or creates an empty image when omitted. */
        constructor(path?: string);
        /** Linear size in bytes of the current pixel buffer. */
        readonly size: number;
        /** Whether the image uses deferred texture upload behavior. */
        readonly delayed: boolean;
        /** CPU pixel buffer; assigning it copies the supplied `ArrayBuffer`. */
        pixels: ArrayBuffer;
        /** CPU palette buffer for indexed images; assigning it copies the buffer. */
        palette: ArrayBuffer;
        /** Texture width in pixels. Set before assigning pixels for new images. */
        texWidth: number;
        /** Texture height in pixels. Set before assigning pixels for new images. */
        texHeight: number;
        /** Pixel storage format: 4, 8, 16, 24 or 32 bits per pixel. */
        bpp: number;
        /** Texture filter mode, usually a GS nearest/linear constant. */
        filter: number;
        /** Whether dimensions and a valid pixel buffer are available for drawing. */
        renderable: boolean;
        /** Destination draw width in pixels. */
        width: number;
        /** Destination draw height in pixels. */
        height: number;
        /** Source rectangle's left coordinate in texture pixels. */
        startx: number;
        /** Source rectangle's top coordinate in texture pixels. */
        starty: number;
        /** Source rectangle's right coordinate in texture pixels. */
        endx: number;
        /** Source rectangle's bottom coordinate in texture pixels. */
        endy: number;
        /** Rotation angle in radians used by `draw()`. */
        angle: number;
        /** Packed RGBA tint multiplied with sampled texture color. */
        color: number;

        /** True when the image has valid dimensions and CPU pixel data. */
        ready(): boolean;
        /** Queues a textured sprite at `(x, y)` for the current frame. */
        draw(x: number, y: number, options?: ImageDrawOptions): void;
        /** Uploads the image synchronously and pins its VRAM allocation. */
        lock(): boolean;
        /** Allows the texture manager to evict the image from VRAM. */
        unlock(): boolean;
        /** Returns whether the image is currently pinned in VRAM. */
        locked(): boolean;
        /** Converts supported 24-bit textures to a PS2-native 16-bit format. */
        optimize(): boolean;
        /** Releases the native image and its CPU/VRAM resources. */
        free(): void;

        /** Copies a rectangular VRAM region between two resident images. */
        static copyVRAMBlock(
            source: Image,
            sourceX: number,
            sourceY: number,
            destination: Image,
            destinationX: number,
            destinationY: number
        ): void;
    }
}
