/**
 * VU1-accelerated batched sprite and tilemap rendering.
 *
 * A `Descriptor` holds textures and materials; an `Instance` pairs one with a
 * native sprite buffer. Sprites are streamed to a VU1 microprogram in
 * batches, so thousands of quads cost little EE time. Draws are queued like
 * any other drawing; call `Screen.flip()` to present them.
 *
 * @example
 * ```js
 * const descriptor = new TileMap.Descriptor({
 *     textures: ["tiles.png"],
 *     materials: [{ textureIndex: 0, endOffset: 1 }],
 * });
 * const map = new TileMap.Instance({
 *     descriptor,
 *     spriteBuffer: TileMap.SpriteBuffer.fromObjects([
 *         { x: 0, y: 0, w: 32, h: 32, u2: 32, v2: 32 },
 *         { x: 32, y: 0, w: 32, h: 32, u1: 32, u2: 64, v2: 32 },
 *     ]),
 * });
 *
 * while (true) {
 *     Screen.clear();
 *     map.render(0, 0);
 *     Screen.flip();
 * }
 * ```
 */
declare namespace TileMap {
    /**
     * One draw state for a contiguous run of sprites. Material `i` draws the
     * sprites after material `i - 1`'s `endOffset` up to and including its
     * own `endOffset`.
     */
    interface Material {
        /**
         * Index into the descriptor's textures, or -1 for untextured
         * sprites. Defaults to 0 when the descriptor has textures, otherwise
         * -1.
         */
        textureIndex?: number;
        /**
         * Alpha blend equation from `Screen.alphaEquation()`. Defaults to
         * the current `Screen` equation at render time.
         */
        blendMode?: number;
        /** Index of the last sprite this material draws. Must not decrease. */
        endOffset: number;
    }

    /**
     * Tileset geometry. Tile `id` is the cell at column `id % columns`, row
     * `Math.floor(id / columns)` of the atlas texture.
     */
    interface Atlas {
        tileWidth: number;
        tileHeight: number;
        columns: number;
        /** When set, tile ids must be below `columns * rows`. */
        rows?: number;
    }

    interface DescriptorOptions {
        /** Required by `Instance.fromGrid()` and `Instance.setTiles()`. */
        atlas?: Atlas;
        /**
         * Textures by path or `Image`. Paths are loaded synchronously. An
         * `Image` that is still loading (for example from an `ImageList`) or
         * was freed skips the sprites of its materials until it is ready.
         */
        textures?: Array<string | Image>;
        /** At least one material, ordered by `endOffset`. */
        materials: Material[];
    }

    /** Immutable render description shared by any number of instances. */
    class Descriptor {
        constructor(options: DescriptorOptions);
        readonly materialCount: number;
        /** The `Image` objects the descriptor keeps alive. */
        readonly textures: Image[];
        readonly atlas: Atlas | undefined;
    }

    /** Tile id that hides a cell: `setTiles`/`fromGrid` give it zero size. */
    const EMPTY: number;

    /** Tile ids: a `Uint16Array` is used without copying. */
    type TileIds = Uint16Array | Int16Array | number[];

    interface GridOptions {
        /** Descriptor with an `atlas`. */
        descriptor: Descriptor;
        columns: number;
        rows: number;
        /** Row-major tile ids, `columns * rows` long; default all 0. */
        tiles?: TileIds;
        /** Cell size on screen; defaults to the atlas tile size. */
        tileWidth?: number;
        tileHeight?: number;
        zindex?: number;
    }

    interface RenderOptions {
        /** Draw only sprites [first, first + count). Disables culling. */
        first?: number;
        count?: number;
        /**
         * Grid instances draw only the cells on screen (plus one cell of
         * margin) by default; `false` draws every cell.
         */
        cull?: boolean;
    }

    /**
     * Native sprite storage. Buffers that are rendered must be 16-byte
     * aligned; `SpriteBuffer.create()` and `fromObjects()` always are. A
     * plain `new ArrayBuffer()` may not be.
     */
    type SpriteStorage = ArrayBuffer | ArrayBufferView;

    interface InstanceOptions {
        descriptor: Descriptor;
        /** Sprite records laid out as described by `TileMap.layout`. */
        spriteBuffer?: SpriteStorage;
    }

    /** A descriptor plus a sprite buffer that can be rendered. */
    class Instance {
        constructor(options: InstanceOptions);
        /**
         * Builds a row-major grid in native code: cell (column, row) is
         * sprite `row * columns + column` at (column * tileWidth,
         * row * tileHeight). Grid instances cull to the screen in
         * `render()`. Culling assumes cells stay near their position: a
         * sprite moved more than one cell away may be skipped.
         */
        static fromGrid(options: GridOptions): Instance;
        readonly descriptor: Descriptor;
        /** Sprites in the current buffer, or 0 without a buffer. */
        readonly spriteCount: number;
        /** Sprites queued by the last `render()`, after culling. */
        readonly lastDrawCount: number;
        /** Grid geometry for `fromGrid()` instances, else undefined. */
        readonly grid: { columns: number; rows: number;
            tileWidth: number; tileHeight: number } | undefined;
        /**
         * Queues sprites at (x, y) plus the camera offset. Sprites are read
         * when the frame is sent, so writes made to the buffer after
         * `render()` and before `Screen.flip()` may or may not be shown this
         * frame.
         */
        render(x: number, y: number, options?: RenderOptions): void;
        /** Moves sprites [first, first + count) in native code. */
        translate(first: number, count: number, dx: number, dy: number): void;
        /** Sets the color (0-255, 128 = neutral) of a sprite range. */
        setColor(first: number, count: number, r: number, g: number,
            b: number, a?: number): void;
        /**
         * Points sprites from `first` at atlas tiles, one per id, and sets
         * their size to the cell (grid) or atlas tile size; `EMPTY` hides a
         * sprite. All ids are validated before anything is written.
         */
        setTiles(first: number, tiles: TileIds): void;
        /**
         * Uses another buffer from now on. Waits for queued draws that read
         * the previous one, so replacing buffers mid-frame stalls briefly.
         */
        replaceSpriteBuffer(buffer: SpriteStorage): void;
        /** Returns the current buffer; edits through a `DataView` are live. */
        getSpriteBuffer(): SpriteStorage | undefined;
        /**
         * Copies `count` sprites (default: all of `source`) into the buffer
         * starting at sprite `dstOffset`. `source` needs no alignment.
         */
        updateSprites(dstOffset: number, source: SpriteStorage,
            count?: number): void;
    }

    /** Fields accepted by `SpriteBuffer.fromObjects()`. */
    interface SpriteObject {
        x?: number;
        y?: number;
        w?: number;
        h?: number;
        /** Texture coordinates in texels. */
        u1?: number;
        v1?: number;
        u2?: number;
        v2?: number;
        /**
         * Depth. The VU program passes the raw bits of this float to the
         * GS, so it orders correctly only for non-negative values and a
         * 24- or 32-bit Z buffer; with the default 16-bit Z buffer, depth
         * testing tiles is unreliable.
         */
        zindex?: number;
        /** Color channels 0-255; default 128 (0x80, neutral modulation). */
        r?: number;
        g?: number;
        b?: number;
        a?: number;
    }

    namespace SpriteBuffer {
        /** Allocates `count` zeroed sprites. */
        function create(count: number): ArrayBuffer;
        /** Builds a buffer from objects; missing fields are 0 (colors 128). */
        function fromObjects(sprites: SpriteObject[]): ArrayBuffer;
    }

    /** Byte layout of one sprite record, for `DataView` access. */
    const layout: {
        readonly stride: number;
        readonly offsets: {
            readonly x: number;
            readonly y: number;
            readonly w: number;
            readonly h: number;
            readonly u1: number;
            readonly v1: number;
            readonly u2: number;
            readonly v2: number;
            /** Colors are 32-bit unsigned integers. */
            readonly r: number;
            readonly g: number;
            readonly b: number;
            readonly a: number;
            readonly zindex: number;
        };
    };

    /**
     * Hardware debugging switches, for bisecting problems that appear only
     * on a real console. They slow rendering; leave them off otherwise.
     */
    interface Diagnostics {
        /** FLUSHA and TEX0/TEX1 before every batch, as the old renderer. */
        flushEachBatch: boolean;
        /** Write back the whole data cache instead of the sprite range. */
        fullCacheFlush: boolean;
        /** Sprites per VU1 batch, 1-50 (default 50). */
        batchSize: number;
    }

    /** Changes the given switches; the others keep their current value. */
    function setDiagnostics(options: Partial<Diagnostics>): void;
    function getDiagnostics(): Diagnostics;

    /** Offsets every instance drawn afterwards; defaults to (0, 0). */
    function setCamera(x: number, y: number): void;
    function getCamera(): { x: number; y: number };
}
