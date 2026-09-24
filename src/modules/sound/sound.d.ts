/**
 * Audio through audsrv: short ADPCM sound effects on the 24 SPU2 voices and
 * one streamed music track (WAV or Ogg Vorbis).
 *
 * There is nothing to enable: the audsrv and libsd IOP drivers are loaded the
 * first time a sound is created or played. `audsrv = true` in athena.ini is
 * still accepted and loads them at boot instead.
 *
 * Streams:
 * - one plays at a time; `play()` on another stream replaces it;
 * - PCM WAV (8/16-bit) and Ogg Vorbis, mono or stereo, at 11025, 12000,
 *   22050, 24000, 32000, 44100 or 48000 Hz (some rates only as 16-bit or
 *   stereo; the constructor throws for unsupported combinations);
 * - `pause()` keeps the position heard, so `play()` resumes exactly there;
 * - decoding runs on a worker thread, so the frame loop only has to keep
 *   calling `Screen.flip()` (or otherwise block) for audio to flow.
 *
 * Sound effects:
 * - `.adp` files with an APCM header, as written by `adpenc`
 *   (`adpenc -L` for a looping sample);
 * - uploaded to SPU2 RAM (~2 MiB shared by every sample) and freed with
 *   `free()` or by the garbage collector;
 * - `IOP.reset()` unloads them: playing one afterwards throws, load it again.
 *
 * Failures throw: `TypeError` for wrong argument types, `RangeError` for
 * values out of range, `InternalError` for I/O, format or IOP failures.
 *
 * @example
 * ```js
 * const music = Sound.Stream("music/theme.ogg");
 * music.loop = true;
 * music.play();
 *
 * const jump = new Sound.Sfx("sfx/jump.adp");
 * jump.volume = 80;
 * jump.pan = -30;
 *
 * while (true) {
 *     pad.update();
 *     if (pad.justPressed(Pads.CROSS)) jump.play();
 *     if (pad.justPressed(Pads.START)) music.playing() ? music.pause() : music.play();
 *     Screen.flip();
 * }
 * ```
 */
declare namespace Sound {
    /** Number of SPU2 voices available to sound effects (24). */
    const CHANNELS: number;

    /** Sets the music stream volume, an integer from 0 to 100 (default 100). */
    function setVolume(volume: number): void;
    /** Music stream volume set with `setVolume()`. */
    function getVolume(): number;
    /** A channel (0-23) no sound effect is playing on, or -1 if all are busy. */
    function findChannel(): number;

    /** A WAV or Ogg Vorbis file streamed from storage while it plays. */
    class Stream {
        /** Opens `path`; also callable without `new`. Does not start playback. */
        constructor(path: string);
        /** Starts, or resumes from `position`. Stops the stream that was playing. */
        play(): void;
        /** Pauses at the position heard. */
        pause(): void;
        /** Pauses and rewinds to the start. */
        stop(): void;
        /** True from `play()` until paused, stopped or finished. */
        playing(): boolean;
        /** Moves to the start; keeps playing if it was. */
        rewind(): void;
        /** Closes the file. Using the object afterwards throws. */
        free(): void;
        /** Restart from the beginning at the end instead of stopping. */
        loop: boolean;
        /** Playback position in milliseconds; assigning seeks (clamped to 0..length). */
        position: number;
        /** Duration in milliseconds. */
        readonly length: number;
        /** Sample rate in Hz. */
        readonly rate: number;
        /** 1 (mono) or 2 (stereo). */
        readonly channels: number;
        readonly format: 'wav' | 'ogg';
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
        /** Whether this sample is still playing on `channel`. */
        playing(channel: number): boolean;
        /** Releases the SPU2 memory. Using the object afterwards throws. */
        free(): void;
        /** 0 to 100, applied on the next `play()`. Default 100. */
        volume: number;
        /** -100 (left) to 100 (right), applied on the next `play()`. Default 0. */
        pan: number;
        /** Whether the sample was encoded to loop (read-only). */
        readonly loop: boolean;
        /**
         * Always 0. audsrv plays samples at the rate they were encoded with;
         * assigning throws.
         */
        readonly pitch: number;
        /** Duration in milliseconds. */
        readonly length: number;
        /** Sample rate in Hz. */
        readonly rate: number;
    }
}
