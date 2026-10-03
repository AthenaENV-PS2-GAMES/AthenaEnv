/**
 * Packs RGBA components into the 32-bit color format used by AthenaEnv.
 *
 * The component order in the returned value is `0xAABBGGRR`:
 * red occupies the least-significant byte and alpha the most-significant.
 * Component values are converted to unsigned 8-bit values.
 *
 * The default alpha used by `new()` is `0x80`, matching the PS2 GS default
 * convention. Color helpers are pure and return a new packed value.
 *
 * @example
 * ```js
 * let tint = Color.new(255, 128, 0, 255);
 * tint = Color.setA(tint, 192);
 * console.log(Color.getR(tint), Color.getA(tint));
 * ```
 */
declare namespace Color {
    /** Packed `0xAABBGGRR` color value. */
    type Value = number;

    /** Creates a packed color from red, green, blue and optional alpha. */
    function new(r: number, g: number, b: number, a?: number): Value;
    /** Reads the red component in the range 0..255. */
    function getR(color: Value): number;
    /** Reads the green component in the range 0..255. */
    function getG(color: Value): number;
    /** Reads the blue component in the range 0..255. */
    function getB(color: Value): number;
    /** Reads the alpha component in the range 0..255. */
    function getA(color: Value): number;
    /** Returns `color` with its red component replaced. */
    function setR(color: Value, value: number): Value;
    /** Returns `color` with its green component replaced. */
    function setG(color: Value, value: number): Value;
    /** Returns `color` with its blue component replaced. */
    function setB(color: Value, value: number): Value;
    /** Returns `color` with its alpha component replaced. */
    function setA(color: Value, value: number): Value;
}
