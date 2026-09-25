/**
 * Audio through audsrv: short ADPCM sound effects on the 24 SPU2 voices and
 * one streamed music track (WAV or Ogg Vorbis).
 *
 * There is nothing to enable: the audsrv and libsd IOP drivers are loaded the
 * first time a sound is created or played. `audsrv = true` in athena.ini is
 * still accepted and loads them at boot instead.
 *
 * Streams:
 * - one plays at a time; `play()` on another stream replaces it (there is no
 *   crossfade: audsrv has a single stream voice);
 * - WAV (PCM 8/16/24/32-bit or 32-bit float) and Ogg Vorbis, mono or stereo,
 *   1 to 192 kHz. What audsrv cannot play as is (e.g. 16 kHz, 8-bit stereo,
 *   float) is converted on the EE while it plays; see `converted`;
 * - `pause()` keeps the position heard, so `play()` resumes exactly there;
 * - seeking, or switching to a stream of the same format, has no gap: the
 *   new audio follows the ~0.1 s audsrv already holds (other formats pause
 *   ~0.15 s while audsrv is reconfigured);
 * - `play`, `pause` and `stop` take `{ fade: ms }` for smooth fades;
 * - a reader thread decodes up to 0.5 s ahead, so slow storage (USB, disc)
 *   does not interrupt the music; the frame loop only has to keep calling
 *   `Screen.flip()` (or otherwise block) for audio to flow;
 * - `onEnd`/`onLoop` run inside `Sound.process()`; call it once per frame.
 *
 * Sound effects:
 * - `.adp` files with an APCM header, made with `node tools/wav2adp.js`
 *   (or `adpenc`; `-L` for a looping sample). Files that would make the
 *   SPU2 play past their end are refused (`CORRUPT`);
 * - uploaded to SPU2 RAM (~2 MiB shared by every sample, see
 *   `getMemoryStats()`) and freed with `free()` or by the garbage collector;
 * - `loadSfxAsync()` reads the file on a worker thread, so a big sample
 *   does not stall the frame;
 * - after `IOP.reset()` a sample is uploaded again from its file the next
 *   time it plays.
 *
 * Failures throw with a stable `error.code` (see `ErrorCode`): `TypeError`
 * for wrong argument types, `RangeError` for values out of range,
 * `InternalError` for I/O, format or IOP failures.
 *
 * @example
 * ```js
 * const music = Sound.Stream("music/theme.ogg");
 * music.loop = true;
 * music.onLoop = () => console.log("theme looped");
 * music.play({ fade: 1000 });
 *
 * const jump = new Sound.Sfx("sfx/jump.adp");
 * jump.volume = 80;
 * jump.pan = -30;
 *
 * while (true) {
 *     pad.update();
 *     if (pad.justPressed(Pads.CROSS)) jump.play();
 *     if (pad.justPressed(Pads.START)) music.playing() ? music.pause({ fade: 300 }) : music.play();
 *     Sound.process();
 *     Screen.flip();
 * }
 * ```
 */
declare namespace Sound {
    type ErrorCode =
        | 'INVALID_ARGUMENT'
        /** The file could not be opened. */
        | 'NOT_FOUND'
        | 'IO'
        /** Not a WAV/OGG/APCM file, or an encoding that cannot be played. */
        | 'BAD_FORMAT'
        /** ADPCM data the SPU2 would play past its end (truncated file). */
        | 'CORRUPT'
        | 'NO_MEMORY'
        /** Not enough SPU2 memory (or IOP heap) for the sample; see getMemoryStats(). */
        | 'SPU_MEMORY'
        /** audsrv could not be loaded or started on the IOP. */
        | 'IOP'
        /** The streaming thread could not be started. */
        | 'THREAD'
        /** Sfx.pitch, or assigning Sfx.loop. */
        | 'UNSUPPORTED'
        /** The object was used after free(). */
        | 'FREED'
        /** The loadSfxAsync() job was cancelled. */
        | 'CANCELLED';

    interface Error {
        code: ErrorCode;
        message: string;
    }

    /** SPU2 sample memory in bytes. */
    interface MemoryStats {
        /** Sample memory in SPU2 RAM (~2 MiB). */
        total: number;
        /** From the start of sample memory to the end of the last sample. */
        used: number;
        /** After the last sample: the largest sample that still fits. */
        free: number;
        /**
         * Freed but not reusable yet: audsrv only reclaims memory at the end,
         * so a sample freed before later ones leaves a hole until those are
         * freed too. Load long-lived samples first.
         */
        wasted: number;
        /** Samples loaded. */
        samples: number;
    }

    interface FadeOptions {
        /** Milliseconds, 0 to 60000. Default 0 (immediate). */
        fade?: number;
    }

    /** Number of SPU2 voices available to sound effects (24). */
    const CHANNELS: number;

    /** Sets the music stream volume, an integer from 0 to 100 (default 100). */
    function setVolume(volume: number): void;
    /** Music stream volume set with `setVolume()`. */
    function getVolume(): number;
    /**
     * Scales every sound effect's volume, 0 to 100 (default 100). Voices
     * still sounding follow at once.
     */
    function setSfxVolume(volume: number): void;
    function getSfxVolume(): number;
    /** A channel (0-23) no sound effect is playing on, or -1 if all are busy. */
    function findChannel(): number;
    /** SPU2 sample memory use. */
    function getMemoryStats(): MemoryStats;
    /**
     * Runs the `onLoop`/`onEnd` callbacks of streams that looped or ended
     * since the last call, each at most once per call, and returns how many
     * ran. An exception thrown by a callback propagates.
     */
    function process(): number;

    /**
     * Opaque handle of a `loadSfxAsync()` job. Dropping it cancels the job
     * (and frees the sample if nobody took it from `poll()`).
     */
    interface Job<T> {
        readonly __brand: 'SoundJob';
        readonly __result?: T;
    }

    type JobState = 'running' | 'done' | 'failed' | 'cancelled';

    interface JobStatus<T> {
        state: JobState;
        /** When `state` is `'done'`. The same object on every later poll. */
        result?: T;
        /** When `state` is `'failed'` or `'cancelled'`. */
        error?: Error;
    }

    /**
     * Starts loading a sound effect: a worker thread reads and checks the
     * file while the frame loop runs, then the `poll()` that sees it read
     * uploads it to SPU2 memory (a short DMA, on the script thread).
     *
     * @example
     * ```js
     * const job = Sound.loadSfxAsync("sfx/explosion.adp");
     * // each frame:
     * const status = Sound.poll(job);
     * if (status.state === "done") boom = status.result;
     * ```
     */
    function loadSfxAsync(path: string): Job<Sfx>;
    /** The job's state without blocking; uploads the sample once it was read. */
    function poll<T>(job: Job<T>): JobStatus<T>;
    /**
     * Blocks until the job is no longer running or `timeoutMs` passes
     * (default: no limit), letting other threads run meanwhile, then
     * returns `poll(job)`.
     */
    function wait<T>(job: Job<T>, timeoutMs?: number): JobStatus<T>;
    /** The job ends as `'cancelled'` unless it already finished. */
    function cancel(job: Job<unknown>): void;

    /** A WAV or Ogg Vorbis file streamed from storage while it plays. */
    class Stream {
        /** Opens `path`; also callable without `new`. Does not start playback. */
        constructor(path: string);
        /**
         * Starts, or resumes from `position`. Stops the stream that was
         * playing. With `fade` it starts silent and rises to full volume;
         * during a fade-out it cancels the fade.
         */
        play(options?: FadeOptions): void;
        /**
         * Pauses at the position heard. With `fade` it keeps playing (and
         * `playing()` stays true) until the fade-out ends.
         */
        pause(options?: FadeOptions): void;
        /** Pauses and rewinds to the start, after the fade-out if any. */
        stop(options?: FadeOptions): void;
        /** True from `play()` until paused, stopped, or its last sample is heard. */
        playing(): boolean;
        /** Moves to the start; keeps playing if it was. */
        rewind(): void;
        /** Closes the file. Using the object afterwards throws `FREED`. */
        free(): void;
        /** Restart from the beginning at the end instead of stopping. */
        loop: boolean;
        /**
         * Playback position heard, in milliseconds; assigning seeks (clamped
         * to 0..length). Right after a seek it reads the target, and starts
         * moving once the new audio is heard (~0.1 s later).
         */
        position: number;
        /**
         * Called by `Sound.process()` after the stream's last sample was
         * heard (without `loop`); `this` is the stream.
         */
        onEnd: ((this: Stream) => void) | null;
        /** Called by `Sound.process()` after a looping stream was heard wrapping around. */
        onLoop: ((this: Stream) => void) | null;
        /**
         * The stream's last sample was heard (without `loop`); cleared by
         * `play()`, a seek or `rewind()`.
         */
        readonly ended: boolean;
        /** Duration in milliseconds. */
        readonly length: number;
        /** Sample rate of the file in Hz. */
        readonly rate: number;
        /** 1 (mono) or 2 (stereo). */
        readonly channels: number;
        readonly format: 'wav' | 'ogg';
        /**
         * audsrv cannot play the file's format, so it is converted to 16-bit
         * at a supported rate on the EE (a little CPU while playing).
         */
        readonly converted: boolean;
    }

    /** An ADPCM sample resident in SPU2 memory. */
    class Sfx {
        /** Loads and uploads `path` (.adp); also callable without `new`. */
        constructor(path: string);
        /**
         * Plays on `channel` (0-23), or on any free channel when omitted.
         * Returns the channel used, or -1 when that channel (or every channel)
         * is busy. The volume and pan are applied to the channel first.
         */
        play(channel?: number): number;
        /**
         * Whether this sample is still playing on `channel`. A looping
         * sample plays until `stop()`, `free()` or `IOP.reset()`.
         */
        playing(channel: number): boolean;
        /**
         * Silences this sample on `channel`, or on every channel it plays on.
         * audsrv cannot key a voice off, so it is muted: `playing()` turns
         * false at once and the channel is free for the next `play()`.
         */
        stop(channel?: number): void;
        /**
         * Releases the SPU2 memory. Using the object afterwards throws.
         * Voices still playing this sample are stopped as with `stop()`.
         */
        free(): void;
        /** 0 to 100, applied on the next `play()`. Default 100. */
        volume: number;
        /** -100 (left) to 100 (right), applied on the next `play()`. Default 0. */
        pan: number;
        /** Whether the sample was encoded to loop (`wav2adp -L`); read-only. */
        readonly loop: boolean;
        /**
         * Always 0. audsrv plays samples at the rate they were encoded with;
         * assigning throws `UNSUPPORTED`.
         */
        readonly pitch: number;
        /** Duration in milliseconds. */
        readonly length: number;
        /** Sample rate in Hz. */
        readonly rate: number;
    }
}
