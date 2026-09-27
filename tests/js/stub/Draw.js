/* Host stand-in for Draw: records every call in globalThis.__debugCalls. */
const log = (...call) => (globalThis.__debugCalls ||= []).push(call);

export function rect(x, y, width, height, color) {
    if (!(width >= 1 && height >= 1))
        throw new RangeError("Draw.rect width and height must be at least 1");
    log("rect", x, y, width, height, color);
}
export function line(x1, y1, x2, y2, color) { log("line", x1, y1, x2, y2, color); }
export function circle(x, y, radius, color, filled) { log("circle", x, y, radius, color, !!filled); }
export function point(x, y, color) { log("point", x, y, color); }
