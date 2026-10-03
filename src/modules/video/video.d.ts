/** Stable `error.code` of the InternalError thrown by `new Video()` and `Video.probe()`. */
type VideoErrorCode =
    /** The file could not be opened. */
    | "open_failed"
    /** Another Video is still open; `free()` it first. */
    | "busy"
    /** Not an MPEG-1/2 elementary video stream, or no decodable picture. */
    | "invalid_format"
    /** Valid stream the decoder cannot play: over 1024x1024, not 4:2:0 or a forbidden frame rate. */
    | "unsupported_format"
    /** Not enough memory for the decoder buffers. */
    | "out_of_memory"
    /** The decoder thread could not be started (constructor only). */
    | "thread_failed";

/** Options of `Video.draw(x, y, options)`. */
type VideoDrawOptions = {
    /** Destination width in pixels; 0 or omitted uses the source rectangle's width. */
    width?: number;
    /** Destination height in pixels; 0 or omitted uses the source rectangle's height. */
    height?: number;
    /** Source rectangle's left edge in picture pixels (default 0). */
    startx?: number;
    /** Source rectangle's top edge in picture pixels (default 0). */
    starty?: number;
    /** Source rectangle's right edge; 0 or omitted is the picture's width. */
    endx?: number;
    /** Source rectangle's bottom edge; 0 or omitted is the picture's height. */
    endy?: number;
    /** Rotation in radians. */
    angle?: number;
    /** Packed RGBA tint from `Color.new()`; alpha 64 draws half transparent. */
    color?: number;
};

/**
 * What `Video.audio` uses of its stream. `Sound.Stream` has all of it;
 * spelled out so this module does not depend on Sound.
 */
type VideoAudioSource = {
    /** Milliseconds heard. */
    readonly position: number;
    readonly ended: boolean;
    loop: boolean;
    play(): void;
    pause(): void;
    stop(): void;
    rewind(): void;
};

/** Stream information returned by `Video.probe()`. */
type VideoInfo = {
    /** Picture size in pixels. */
    width: number;
    height: number;
    /** Decoded texture size: the picture rounded up to 16x16 macroblocks. */
    codedWidth: number;
    codedHeight: number;
    /** Frame rate; 0 when the header holds a forbidden value. */
    fps: number;
    /** Whole frames in the file. */
    frames: number;
    /** `frames / fps`, in seconds; 0 when `fps` is 0. */
    duration: number;
    /** MPEG-2 (true) or MPEG-1 (false). */
    mpeg2: boolean;
    progressive: boolean;
    chroma: "4:2:0" | "4:2:2" | "4:4:4" | "unknown";
    /** Whether `new Video()` accepts the stream. */
    supported: boolean;
};

/**
 * MPEG-1/2 video playback decoded by the PS2's IPU.
 *
 * `Video` reads a raw MPEG-1/2 elementary video stream (`.m2v`, video only,
 * 4:2:0 chroma, no container and no audio). Frames are decoded at the
 * stream's own frame rate by `update()` and drawn with `draw()` or through
 * the `frame` Image.
 *
 * The decoder is a single hardware resource: only one `Video` can be open at
 * a time. Call `free()` before opening another one.
 *
 * Supported streams: 4:2:0 chroma, at most 1024x1024 (the largest GS
 * texture) and a standard frame rate. Others are refused on construction.
 *
 * Encode a compatible file with:
 * `ffmpeg -i input.mp4 -vf scale=640:360 -c:v mpeg2video -b:v 2000k -g 15 -an video.m2v`
 *
 * @example
 * ```js
 * const video = new Video('video.m2v');
 * video.play();
 * while (!video.ended) {
 *     Screen.clear(Color.new(0, 0, 0));
 *     video.update();
 *     video.draw(0, 0, 640, 448);
 *     Screen.flip();
 * }
 * video.free();
 * ```
 */
declare class Video {
    /**
     * Opens `path` and decodes its first frame.
     * Throws an InternalError with a `code` (see `VideoErrorCode`) when the
     * file cannot be opened or played, or another Video is still open.
     */
    constructor(path: string);

    /**
     * Reads the stream's headers without opening the decoder, so it works
     * while another Video is open. Reads the whole file to count frames,
     * which takes a while on disc. Throws like the constructor
     * (`open_failed`, `invalid_format`, `out_of_memory`); an unplayable
     * stream is reported by `supported` instead.
     */
    static probe(path: string): VideoInfo;

    /** Picture width in pixels. */
    readonly width: number;
    /**
     * Picture height in pixels. The decoded texture is rounded up to whole
     * 16x16 macroblocks (`frame.texHeight`); the padding is never drawn.
     */
    readonly height: number;
    /** Frame rate read from the sequence header. */
    readonly fps: number;
    /** True once the first frame has been decoded. */
    readonly ready: boolean;
    /** True once playback reached the end of the stream while not looping. */
    readonly ended: boolean;
    /** True while playing (not stopped, paused or ended). */
    readonly playing: boolean;
    /**
     * Restart from the beginning instead of ending. With `audio` set, it also
     * sets the stream's `loop`, and the video restarts when the audio wraps.
     */
    loop: boolean;
    /** Index of the picture shown: 0 is the first, again after a rewind or loop. */
    readonly currentFrame: number;
    /** Times playback looped; reset by `stop()` and by `play()` after the end. */
    readonly loopCount: number;
    /**
     * The current frame as an Image that follows playback; the same object
     * is returned on every access, sized to the picture. It borrows the
     * decoder's buffer: setting its pixels, palette, bpp, texWidth or
     * texHeight throws a TypeError and `optimize()` returns false. Once the
     * Video is freed it is no longer loaded: `ready()` is false and
     * `draw()` throws.
     */
    readonly frame: Image | null;
    /**
     * Audio track playback follows (lip sync), usually a `Sound.Stream` of
     * the video's soundtrack, or null. While set:
     * - the picture shown is picked from `audio.position` (the time heard),
     *   so audio stalls, pauses and emulator speed keep both in step;
     * - `play()`, `pause()` and `stop()` also drive the stream (`play()`
     *   rewinds it when the video starts over), and `loop` sets its `loop`;
     * - a looping video restarts when the audio wraps; one shorter than its
     *   audio holds its last picture until then;
     * - once the audio has ended, the remaining pictures play on the EE
     *   clock, so the video always ends.
     *
     * Can only be changed while the video is not playing. The stream is not
     * freed or stopped by `free()`. Do not seek the stream while attached:
     * seeking back restarts the video, and seeking ahead makes it decode
     * every picture up to there.
     */
    audio: VideoAudioSource | null;

    /**
     * Called by `update()` when playback reaches the end while not looping,
     * with the Video as `this`. An exception it throws is thrown by
     * `update()`. Setting a non-function other than undefined/null makes
     * `update()` throw a TypeError when the event happens.
     */
    onEnd?: (() => void) | null;
    /**
     * Called by `update()` each time a looping video restarts, with the new
     * `loopCount`. Same rules as `onEnd`.
     */
    onLoop?: ((loopCount: number) => void) | null;

    /** Starts or resumes playback. Restarts from the beginning once ended. */
    play(): void;
    /** Pauses on the current frame. */
    pause(): void;
    /** Stops and rewinds to the beginning. */
    stop(): void;
    /**
     * Advances playback by the time elapsed since the last call, following
     * the stream's frame rate whatever the render rate. When the loop falls
     * behind, late frames are decoded and skipped (up to 3 per call). Runs
     * `onLoop`/`onEnd`. Call once per rendered frame. Returns true when the
     * picture changed.
     */
    update(): boolean;
    /**
     * Draws the current frame. `width`/`height` of 0 (the default) use the
     * picture size.
     */
    draw(x?: number, y?: number, width?: number, height?: number): void;
    /**
     * Draws the current frame with a source rectangle, size, rotation or
     * tint. Throws a RangeError when the source rectangle is empty or goes
     * outside the picture.
     */
    draw(x: number, y: number, options: VideoDrawOptions): void;
    /** Releases the decoder and buffers. The Video cannot be used afterwards. */
    free(): void;
}
