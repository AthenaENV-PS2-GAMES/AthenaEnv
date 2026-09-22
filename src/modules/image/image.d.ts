declare module "Image" {
    type ImageDrawOptions = {
        width?: number;
        height?: number;
        startx?: number;
        starty?: number;
        endx?: number;
        endy?: number;
        angle?: number;
        color?: number;
    };

    class Image {
        constructor(path?: string);
        readonly size: number;
        readonly delayed: boolean;
        pixels: ArrayBuffer;
        palette: ArrayBuffer;
        texWidth: number;
        texHeight: number;
        bpp: number;
        filter: number;
        renderable: boolean;
        width: number;
        height: number;
        startx: number;
        starty: number;
        endx: number;
        endy: number;
        angle: number;
        color: number;

        ready(): boolean;
        draw(x: number, y: number, options?: ImageDrawOptions): void;
        lock(): boolean;
        unlock(): boolean;
        locked(): boolean;
        optimize(): boolean;
        free(): void;

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
