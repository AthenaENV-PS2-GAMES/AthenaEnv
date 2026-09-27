/* Host stand-in for Color.new(): packed 0xAABBGGRR, alpha 0x80 by default. */
export function new_(r, g, b, a = 128) {
    return ((r & 255) | ((g & 255) << 8) | ((b & 255) << 16) | ((a & 255) << 24)) >>> 0;
}
export { new_ as new };
