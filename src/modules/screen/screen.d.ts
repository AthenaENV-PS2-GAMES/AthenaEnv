declare namespace Screen {
    interface VideoMode {
        mode: number;
        width: number;
        height: number;
        psm: number;
        interlace: number;
        field: number;
        psmz: number;
        zbuffering: boolean;
        double_buffering: boolean;
    }

    interface AlphaEquation {
        a: number;
        b: number;
        c: number;
        d: number;
        fix: number;
    }

    interface ScissorBounds {
        x0: number;
        y0: number;
        x1: number;
        y1: number;
    }

    function flip(): void;
    function clear(color?: number): void;
    function waitVblankStart(): void;
    function setVSync(enabled: boolean): void;
    function setFrameCounter(enabled: boolean): void;
    function getMemoryStats(mode?: number): number;
    function getFPS(interval: number): number;
    function getMode(): VideoMode;
    function setMode(mode: VideoMode): void;
    function alphaEquation(a: number, b: number, c: number, d: number,
        fix: number): bigint;
    function getParam(param: number): number | bigint | AlphaEquation | ScissorBounds;
    function setParam(param: number, value: number | bigint | AlphaEquation | ScissorBounds): void;
    function switchContext(): number;
    function flush(): void;

    const VRAM_SIZE: number;
    const VRAM_USED_TOTAL: number;
    const VRAM_USED_STATIC: number;
    const VRAM_USED_DYNAMIC: number;
    const ALPHA_TEST_ENABLE: number;
    const ALPHA_TEST_METHOD: number;
    const ALPHA_TEST_REF: number;
    const ALPHA_TEST_FAIL: number;
    const DST_ALPHA_TEST_ENABLE: number;
    const DST_ALPHA_TEST_METHOD: number;
    const DEPTH_TEST_ENABLE: number;
    const DEPTH_TEST_METHOD: number;
    const ALPHA_BLEND_EQUATION: number;
    const SCISSOR_BOUNDS: number;
    const PIXEL_ALPHA_BLEND_ENABLE: number;
    const COLOR_CLAMP_MODE: number;
    const ALPHA_NEVER: number;
    const ALPHA_ALWAYS: number;
    const ALPHA_LESS: number;
    const ALPHA_LEQUAL: number;
    const ALPHA_EQUAL: number;
    const ALPHA_GEQUAL: number;
    const ALPHA_GREATER: number;
    const ALPHA_NEQUAL: number;
    const ALPHA_FAIL_NO_UPDATE: number;
    const ALPHA_FAIL_FB_ONLY: number;
    const ALPHA_FAIL_ZB_ONLY: number;
    const ALPHA_FAIL_RGB_ONLY: number;
    const DST_ALPHA_ZERO: number;
    const DST_ALPHA_ONE: number;
    const DEPTH_NEVER: number;
    const DEPTH_ALWAYS: number;
    const DEPTH_GEQUAL: number;
    const DEPTH_GREATER: number;
    const SRC_RGB: number;
    const DST_RGB: number;
    const ZERO_RGB: number;
    const SRC_ALPHA: number;
    const DST_ALPHA: number;
    const ALPHA_FIX: number;
    const BLEND_DEFAULT: bigint;
    const BLEND_ADD_NOALPHA: bigint;
    const BLEND_ADD: bigint;
    const NTSC: number;
    const PAL: number;
    const DTV_480p: number;
    const DTV_576p: number;
    const DTV_720p: number;
    const DTV_1080i: number;
    const INTERLACED: number;
    const PROGRESSIVE: number;
    const FIELD: number;
    const FRAME: number;
    const CT32: number;
    const CT24: number;
    const CT16: number;
    const CT16S: number;
    const Z32: number;
    const Z24: number;
    const Z16: number;
    const Z16S: number;
    const DRAW_BUFFER: number;
    const DISPLAY_BUFFER: number;
    const DEPTH_BUFFER: number;
}
