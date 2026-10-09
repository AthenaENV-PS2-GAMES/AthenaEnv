/**
 * Frame-by-frame recording and playback of controller input.
 *
 * Record: capture the inputs once per update step and let the game read the
 * recorder's sources, which hold the values exactly as stored (sticks
 * quantized to 1/127). Play back: read a Playback's sources instead and
 * call advance() once per step. Seed the game's generators with `seed` in
 * both runs, use a fixed step, and the game repeats itself (PCSX2 runs are
 * deterministic), which turns a bug or a benchmark path into a file.
 *
 * Not in the default build: `node tools/modules.js configure --modules=replay,...`
 *
 * Example:
 * ```js
 * const rec = new Replay.Recorder([Gamepad.player(0)], { seed: 1234 });
 * Random.seed(rec.seed);
 * controls.setSource(rec.source(0));
 * // every step: Gamepad.update(); rec.capture(); controls.update(); ...
 * rec.save("host:bug42.rpl");
 *
 * const play = Replay.load("host:bug42.rpl");
 * Random.seed(play.seed);
 * controls.setSource(play.source(0));
 * // every step: if (!play.advance()) Loop.stop(); controls.update(); ...
 * ```
 */
declare namespace Replay {
    const MAX_PLAYERS: 8;
    interface Source { connected: boolean; buttons: number; leftX: number; leftY: number; rightX: number; rightY: number }
    class Recorder {
        /** inputs default to Gamepad player 0. seed: u32 stored in the file; maxFrames default 216000 (1 h at 60 Hz). */
        constructor(inputs?: Input.Source[], options?: { seed?: number; maxFrames?: number });
        readonly seed: number;
        readonly frames: number;
        /** The stored (quantized) snapshot of input i, updated by capture(). */
        source(index?: number): Source;
        /** Records one frame; false once maxFrames is reached. */
        capture(): boolean;
        toArrayBuffer(): ArrayBuffer;
        /** Writes the file; returns its size. */
        save(path: string): number;
    }
    class Playback {
        constructor(data: ArrayBuffer);
        readonly seed: number;
        readonly frames: number;
        readonly players: number;
        /** Frames played so far. */
        readonly frame: number;
        readonly done: boolean;
        source(index?: number): Source;
        /** Loads the next frame into the sources; false when the recording ended (sources released). */
        advance(): boolean;
        restart(): this;
    }
    function load(path: string): Playback;
}
