/**
 * Font loading and text rendering.
 *
 * The constructor optionally accepts a path to either a TrueType file or a legacy
 * bitmap font (`.bmp`, `.png` or `.jpg`, optionally with a `.dat` width file).
 * With no path, the embedded Quicksand Regular font is used. Text is queued
 * into the current graphics command stream.
 */
declare class Font {
    constructor(path?: string);

    static readonly ALIGN_TOP: number;
    static readonly ALIGN_BOTTOM: number;
    static readonly ALIGN_VCENTER: number;
    static readonly ALIGN_LEFT: number;
    static readonly ALIGN_RIGHT: number;
    static readonly ALIGN_HCENTER: number;
    static readonly ALIGN_NONE: number;
    static readonly ALIGN_CENTER: number;

    scale: number;
    color: Color.Value;
    align: number;
    outline: number;
    outline_color: Color.Value;
    dropshadow: number;
    dropshadow_color: Color.Value;

    print(x: number, y: number, text: string): void;
    getTextSize(text: string): { width: number; height: number };
    render(text: string): FontRender;
}

declare class FontRender {
    print(x: number, y: number): void;
}
