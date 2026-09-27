/* Host stand-in for Font: 8x16 glyphs; print() and FontRender.print() recorded in globalThis.__debugCalls. */
const log = (...call) => (globalThis.__debugCalls ||= []).push(call);

export class Font {
    constructor(pathOrOptions, options) {
        this.color = 0x80FFFFFF;
        this.scale = 1;
        this.options = typeof pathOrOptions === "object" ? pathOrOptions : options;
        this.freed = false;
    }
    get lineHeight() { return 16; }
    print(x, y, text) {
        if (this.freed) throw new TypeError("Font has been freed");
        log("print", x, y, text, this.color);
    }
    render(text) {
        const font = this;
        log("render", text);
        return { print(x, y) { font.print(x, y, text); } };
    }
    preload() { return Promise.resolve(this); }
    free() { this.freed = true; }
    getTextSize(text) {
        const lines = String(text).split("\n");
        return { width: 8 * Math.max(...lines.map(line => line.length)), height: 16 * lines.length };
    }
}
