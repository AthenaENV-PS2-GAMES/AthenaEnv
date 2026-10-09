/*
 * Replay: record controller input frame by frame and play it back.
 *
 * A Recorder captures snapshots of sources (Gamepad players or anything with
 * buttons, leftX, leftY, rightX and rightY) once per update step and exposes
 * them through its own sources, quantized exactly as they are stored. The
 * game reads those (e.g. Input.Map.setSource(recorder.source(0))) while
 * recording, and a Playback's sources while playing back, so both runs see
 * identical values. Seed the game's random generators with `seed` in both.
 *
 * Format (little endian): "ARPL", u8 version (1), u8 players, u16 0,
 * u32 seed, u32 frames, then per frame and player u16 buttons and four int8
 * sticks (value * 127, rounded): 6 bytes per player per frame.
 */
const MAGIC = 0x4C505241; /* "ARPL" */
const VERSION = 1;
const HEADER = 16;
const STRIDE = 6;
export const MAX_PLAYERS = 8;

function openFile(path, mode, what) {
    if (typeof std === "undefined" || typeof std.open !== "function") throw new Error(`${what}: std.open is not available`);
    const file = std.open(path, mode);
    if (!file) throw new Error(`${what}: cannot open ${path}`);
    return file;
}

/* Stick value -> int8 (stored as its low byte by the Uint8Array). */
function quantize(v) {
    if (!(v > -1)) return v <= -1 ? -127 : 0;   /* also NaN */
    if (v >= 1) return 127;
    return Math.round(v * 127);
}

/* int8 byte -> stick value: a table, no arithmetic per read. */
const STICK = new Float64Array(256);
for (let b = 0; b < 256; b++) STICK[b] = (b > 127 ? b - 256 : b) / 127;

class Source {
    constructor() { this.connected = true; this.buttons = 0; this.leftX = 0; this.leftY = 0; this.rightX = 0; this.rightY = 0; }
    _read(bytes, at) {
        this.buttons = bytes[at] | (bytes[at + 1] << 8);
        this.leftX = STICK[bytes[at + 2]]; this.leftY = STICK[bytes[at + 3]];
        this.rightX = STICK[bytes[at + 4]]; this.rightY = STICK[bytes[at + 5]];
    }
}

function checkSeed(seed) {
    if (!Number.isInteger(seed) || seed < 0 || seed > 0xFFFFFFFF) throw new RangeError("Replay seed must be an integer from 0 to 0xFFFFFFFF");
    return seed;
}

export class Recorder {
    /**
     * inputs: the sources to capture (default: Gamepad player 0). options.seed
     * (u32, default 0) is stored in the file; options.maxFrames (default
     * 216000, an hour at 60 Hz) caps the recording.
     */
    constructor(inputs, options = {}) {
        if (inputs === undefined) {
            if (!globalThis.Gamepad) throw new Error("Replay.Recorder needs sources or the Gamepad module");
            inputs = [globalThis.Gamepad.player(0)];
        }
        if (!Array.isArray(inputs) || !inputs.length || inputs.length > MAX_PLAYERS)
            throw new RangeError(`Replay.Recorder needs 1 to ${MAX_PLAYERS} sources`);
        for (const s of inputs) if (!s || typeof s.buttons !== "number") throw new TypeError("Replay.Recorder sources need numeric buttons and sticks");
        this.inputs = inputs.slice();
        this.seed = checkSeed(options.seed ?? 0);
        this.maxFrames = options.maxFrames ?? 216000;
        if (!Number.isInteger(this.maxFrames) || this.maxFrames < 1) throw new RangeError("Replay maxFrames must be a positive integer");
        this._sources = this.inputs.map(() => new Source());
        this._bytes = new Uint8Array(1024 * STRIDE * this.inputs.length);
        this.frames = 0;
    }

    /** The quantized snapshot of input i, updated by capture(). */
    source(i = 0) {
        const s = this._sources[i];
        if (!s) throw new RangeError("Replay: no such source");
        return s;
    }

    /** Records one frame from every input; false once maxFrames is reached (nothing recorded). */
    capture() {
        if (this.frames >= this.maxFrames) return false;
        const per = STRIDE * this.inputs.length;
        let at = this.frames * per;
        if (at + per > this._bytes.length) {
            const grown = new Uint8Array(Math.min(this._bytes.length * 2, this.maxFrames * per));
            grown.set(this._bytes); this._bytes = grown;
        }
        const bytes = this._bytes, inputs = this.inputs, sources = this._sources;
        for (let i = 0; i < inputs.length; i++, at += STRIDE) {
            const s = inputs[i];
            if (s.connected === false) {
                bytes[at] = bytes[at + 1] = bytes[at + 2] = bytes[at + 3] = bytes[at + 4] = bytes[at + 5] = 0;
            } else {
                const b = s.buttons;
                bytes[at] = b; bytes[at + 1] = b >> 8;   /* Uint8Array stores the low byte */
                bytes[at + 2] = quantize(s.leftX); bytes[at + 3] = quantize(s.leftY);
                bytes[at + 4] = quantize(s.rightX); bytes[at + 5] = quantize(s.rightY);
            }
            sources[i]._read(bytes, at);
        }
        this.frames++;
        return true;
    }

    /** The recording as an ArrayBuffer. */
    toArrayBuffer() {
        const players = this.inputs.length, size = this.frames * STRIDE * players;
        const buffer = new ArrayBuffer(HEADER + size), view = new DataView(buffer);
        view.setUint32(0, MAGIC, true); view.setUint8(4, VERSION); view.setUint8(5, players);
        view.setUint16(6, 0, true); view.setUint32(8, this.seed, true); view.setUint32(12, this.frames, true);
        new Uint8Array(buffer, HEADER).set(this._bytes.subarray(0, size));
        return buffer;
    }

    /** Writes the recording to a file (std.open). Returns the bytes written. */
    save(path) {
        const buffer = this.toArrayBuffer();
        const file = openFile(path, "wb", "Replay.save");
        try {
            const written = file.write(buffer, 0, buffer.byteLength);
            if (written !== buffer.byteLength) throw new Error(`Replay.save: short write to ${path}`);
        } finally { file.close(); }
        return buffer.byteLength;
    }
}

export class Playback {
    /** From the bytes of a recording (see Recorder.toArrayBuffer()). */
    constructor(buffer) {
        if (!(buffer instanceof ArrayBuffer)) throw new TypeError("Replay.Playback expects an ArrayBuffer");
        if (buffer.byteLength < HEADER) throw new RangeError("Replay: data too short");
        const view = new DataView(buffer);
        if (view.getUint32(0, true) !== MAGIC) throw new RangeError("Replay: not a replay (bad magic)");
        if (view.getUint8(4) !== VERSION) throw new RangeError(`Replay: unsupported version ${view.getUint8(4)}`);
        this.players = view.getUint8(5);
        if (!this.players || this.players > MAX_PLAYERS) throw new RangeError("Replay: bad player count");
        this.seed = view.getUint32(8, true);
        this.frames = view.getUint32(12, true);
        if (buffer.byteLength !== HEADER + this.frames * STRIDE * this.players) throw new RangeError("Replay: size does not match the frame count");
        this._bytes = new Uint8Array(buffer, HEADER);
        this._sources = Array.from({ length: this.players }, () => new Source());
        this.frame = 0;
    }

    /** The snapshot of player i at the current frame, updated by advance(). */
    source(i = 0) {
        const s = this._sources[i];
        if (!s) throw new RangeError("Replay: no such source");
        return s;
    }

    /** Loads the next frame into the sources; false (sources released) once every frame was played. */
    advance() {
        if (this.frame >= this.frames) {
            for (const s of this._sources) { s.buttons = 0; s.leftX = s.leftY = s.rightX = s.rightY = 0; }
            return false;
        }
        let at = this.frame * STRIDE * this.players;
        for (const s of this._sources) { s._read(this._bytes, at); at += STRIDE; }
        this.frame++;
        return true;
    }

    get done() { return this.frame >= this.frames; }
    restart() { this.frame = 0; return this; }
}

/** Reads a recording file (std.open). */
export function load(path) {
    const file = openFile(path, "rb", "Replay.load");
    try {
        file.seek(0, std.SEEK_END);
        const size = file.tell();
        file.seek(0, std.SEEK_SET);
        const buffer = new ArrayBuffer(size);
        if (file.read(buffer, 0, size) !== size) throw new Error(`Replay.load: short read from ${path}`);
        return new Playback(buffer);
    } finally { file.close(); }
}
