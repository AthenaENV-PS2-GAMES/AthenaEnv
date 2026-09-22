/**
 * Display, frame synchronization, VRAM statistics and GS state controls.
 *
 * A typical frame is `Screen.clear()`, drawing commands, then `Screen.flip()`.
 * Most numeric constants are raw PS2 GS values and are intended to be passed
 * back to this module rather than interpreted as application-level units.
 */
declare namespace Screen {
    /** Current video configuration accepted by `getMode()` and `setMode()`. */
    interface VideoMode {
        /** Video mode identifier such as `NTSC` or `PAL`. */
        mode: number;
        /** Visible width in pixels. */
        width: number;
        /** Visible height in pixels. */
        height: number;
        /** Color pixel storage format such as `CT32` or `CT24`. */
        psm: number;
        /** Interlaced/progressive mode. */
        interlace: number;
        /** Field/frame timing mode. */
        field: number;
        /** Depth-buffer pixel storage format. */
        psmz: number;
        /** Enables depth buffering. */
        zbuffering: boolean;
        /** Enables double-buffered presentation. */
        double_buffering: boolean;
        /** Optional rendering pass count; defaults to zero. */
        pass_count?: number;
    }

    /** Arguments for the GS alpha blend equation. */
    interface AlphaEquation {
        a: number;
        b: number;
        c: number;
        d: number;
        fix: number;
    }

    /** Pixel bounds used by the GS scissor register. */
    interface ScissorBounds {
        x0: number;
        y0: number;
        x1: number;
        y1: number;
    }

    /** Presents the completed draw buffer and synchronizes the frame. */
    function flip(): void;
    /** Clears the current draw buffer using a packed RGBA color. */
    function clear(color?: number): void;
    /** Blocks until the next vertical blank starts. */
    function waitVblankStart(): void;
    /** Enables or disables synchronization with vertical blank. */
    function setVSync(enabled: boolean): void;
    /** Enables or disables the on-screen frame counter. */
    function setFrameCounter(enabled: boolean): void;
    /** Returns free VRAM for the selected `VRAM_*` accounting mode. */
    function getMemoryStats(mode?: number): number;
    /** Returns the measured FPS over the requested positive frame interval. */
    function getFPS(interval: number): number;
    /** Returns the active video configuration. */
    function getMode(): VideoMode;
    /** Reconfigures the video mode and render targets. */
    function setMode(mode: VideoMode): void;
    /** Packs the five GS alpha-equation fields into a register value. */
    function alphaEquation(a: number, b: number, c: number, d: number,
        fix: number): bigint;
    /** Reads a supported GS parameter by its `Screen` constant. */
    function getParam(param: number): number | bigint | AlphaEquation | ScissorBounds;
    /** Writes a supported GS parameter by its `Screen` constant. */
    function setParam(param: number, value: number | bigint | AlphaEquation | ScissorBounds): void;
    /** Switches the active GS context and returns its native result code. */
    function switchContext(): number;
    /** Flushes queued graphics commands without presenting a frame. */
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
