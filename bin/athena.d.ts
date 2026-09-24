/**
 * AthenaEnv global JavaScript helpers.
 *
 * These declarations describe the globals available to every AthenaEnv
 * script. They are provided for editor completion and do not require imports.
 *
 * Example:
 * ```js
 * console.log('Loading game');
 * const handle = setTimeout(() => console.log('Ready'), 1000);
 * clearTimeout(handle);
 * ```
 */

declare namespace console {
    /** Writes an informational message to the EE console. */
    function log(...args: any[]): void;
    /** Writes a warning message to the EE console. */
    function warn(...args: any[]): void;
    /** Writes an error message to the EE console. */
    function error(...args: any[]): void;
}

/** Schedules a one-shot callback after `timeout` milliseconds. */
declare function setTimeout(handler: (...args: any[]) => void, timeout?: number, ...args: any[]): any;
/** Schedules a callback repeatedly using the event-loop timer queue. */
declare function setInterval(handler: (...args: any[]) => void, timeout?: number, ...args: any[]): any;
/** Cancels a timeout created by setTimeout. */
declare function clearTimeout(handle?: any): void;
/** Cancels an interval created by setInterval. */
declare function clearInterval(handle?: any): void;
/** Schedules a callback as soon as the event loop becomes idle. */
declare function setImmediate(handler: (...args: any[]) => void, ...args: any[]): any;
/** Cancels a callback created by setImmediate. */
declare function clearImmediate(handle?: any): void;


/* === Module: Mutex (mutex) === */
/**
 * Native EE mutex primitives.
 *
 * Mutexes protect native/application data shared by callbacks or threads.
 * They do not make arbitrary QuickJS runtime access thread-safe; JavaScript
 * execution must still follow AthenaEnv's runtime-gate rules.
 *
 * Example:
 * ```js
 * const lock = Mutex.new();
 * Mutex.lock(lock);
 * try {
 *     // Update shared native/application state.
 * } finally {
 *     Mutex.unlock(lock);
 *     Mutex.destroy(lock);
 * }
 * ```
 */
declare namespace Mutex {
    /** Opaque handle returned by `Mutex.new()`. */
    interface Handle {
        readonly __brand: 'Mutex';
    }

    /** Creates an unlocked native mutex. */
    function new(): Handle;

    /** Blocks until acquired and returns the native EE result code. */
    function lock(mutex: Handle): number;

    /** Releases the mutex and returns the native EE result code. */
    function unlock(mutex: Handle): number;

    /** Releases the native mutex. Do not use `mutex` afterwards. */
    function destroy(mutex: Handle): void;
}


/* === Module: Thread (thread) === */
/**
 * EE thread management.
 *
 * Thread callbacks execute on native EE worker threads. Keep callbacks short,
 * avoid direct QuickJS runtime access from native workers, and use the
 * documented AthenaEnv synchronization boundaries.
 *
 * Example:
 * ```js
 * const worker = Thread.new(() => {
 *     System.delay();
 * }, 'worker', 16384, 16);
 * Thread.start(worker);
 * console.log(Thread.getStatus(worker));
 * Thread.destroy(worker);
 * ```
 */
declare namespace Thread {
    /** Opaque handle returned by `Thread.new()`. */
    interface Handle {
        readonly __brand: 'Thread';
    }

    /** Snapshot of one tracked native thread. */
    interface TaskInfo {
        /** Native EE thread ID. */
        id: number;
        /** Thread name. */
        name: string;
        /** Native status code. */
        status: number;
        /** Configured stack size in bytes. */
        stack: number;
    }

    /**
     * Creates a native EE thread.
     * @param callback Function executed by the new thread.
     * @param name Optional name, limited to 63 characters.
     * @param stackSize Stack size in bytes; defaults to 16384.
     * @param priority EE priority from 1 to 127; defaults to 16.
     */
    function new(
        callback: () => void,
        name?: string,
        stackSize?: number,
        priority?: number
    ): Handle;

    /** Starts execution and returns the native EE result code. */
    function start(thread: Handle): number;

    /** Requests thread termination and returns the native EE result code. */
    function stop(thread: Handle): number;

    /** Returns the native EE thread ID. */
    function getId(thread: Handle): number;

    /** Returns the current thread name. */
    function getName(thread: Handle): string;

    /** Replaces the thread name. */
    function setName(thread: Handle, name: string): void;

    /** Returns the native EE status code. */
    function getStatus(thread: Handle): number;

    /** Releases the thread handle. Stop it first; do not reuse the object. */
    function destroy(thread: Handle): void;

    /** Returns all active/tracked threads. */
    function list(): TaskInfo[];

    /** Force-terminates a native thread by ID. */
    function kill(id: number): number;
}


/* === Module: Archive (archive) === */
/**
 * Read zip, tar, tar.gz and gzip files, and gzip data in memory.
 *
 * Every format is a list of entries: a `.gz` file is a single-entry archive
 * named after the original file. `list`, `read` and `extractAll` work the
 * same way for all of them.
 *
 * Safety:
 * - extraction validates every entry before the first write: names with
 *   `..`, absolute paths or devices (`mc0:`), encrypted entries, existing
 *   files (with `overwrite: false`) and `maxSize` all reject the archive
 *   without writing anything;
 * - a file that fails midway is removed;
 * - `read` and `gunzip` stop at `maxSize` (default 16 MiB), so a small
 *   compressed file cannot exhaust the EE RAM.
 *
 * Failures throw with a stable `error.code` (see `ErrorCode`).
 *
 * Example:
 * ```js
 * // Assets straight from a zip, without extracting to the memory card.
 * const pack = Archive.open("assets.zip");
 * const levelJson = Archive.read(pack, "levels/1.json");
 * Archive.close(pack);
 *
 * // Install with a progress bar; nothing is written if any entry is unsafe.
 * Archive.extract("update.tar.gz", "mass:/GAME", {
 *     overwrite: true,
 *     onProgress(entry, index, count) { drawBar(index / count); },
 * });
 *
 * // Compress a save in memory.
 * const packed = Archive.gzip(saveBytes, { level: 9 });
 * const restored = Archive.gunzip(packed);
 *
 * // Extract on a worker thread while the frame loop keeps drawing.
 * const job = Archive.extractAsync("dlc.zip", "mass:/GAME/dlc");
 * while (true) {
 *     const status = Archive.poll(job);
 *     if (status.state !== "running") break;
 *     drawBar(status.bytesDone / (status.bytesTotal || 1));
 *     Screen.flip();
 * }
 * ```
 *
 * Zip entries must use store or deflate (what nearly every tool writes);
 * other methods fail with `UNSUPPORTED` before anything is written.
 */
declare namespace Archive {
    /** Opaque handle returned by `Archive.open()`. */
    interface Handle {
        readonly __brand: 'Archive';
    }

    type Format = 'zip' | 'tar' | 'gz';

    /** Binary input accepted by `gzip` and `gunzip` (DataView is not supported). */
    type Bytes = ArrayBuffer | Uint8Array | Int8Array | Uint8ClampedArray | Uint16Array | Int16Array
        | Uint32Array | Int32Array | Float32Array | Float64Array;

    type ErrorCode =
        | 'INVALID_ARGUMENT'
        | 'IO'
        | 'BAD_FORMAT'
        | 'NO_MEMORY'
        | 'UNSUPPORTED'
        /** An entry would be written outside the destination. */
        | 'UNSAFE_PATH'
        /** Data is larger than `maxSize`. */
        | 'TOO_LARGE'
        | 'NOT_FOUND'
        /** `overwrite: false` and the destination file exists. */
        | 'EXISTS'
        | 'ENCRYPTED'
        /** `onProgress` returned `false`. An exception thrown by a callback propagates as is. */
        | 'ABORTED'
        /** The handle was used after `close()`. */
        | 'CLOSED'
        /** The handle is in use by another operation (e.g. from a callback). */
        | 'BUSY';

    interface Error extends globalThis.Error {
        code: ErrorCode;
    }

    interface Entry {
        /** Path inside the archive. Directories end with `/`. */
        name: string;
        /** Uncompressed size in bytes. For `.gz` it comes from the file trailer (modulo 4 GiB). */
        size: number;
        /** Stored size in bytes. */
        compressedSize: number;
        /** Modification time as Unix seconds, 0 when unknown. */
        mtime: number;
        dir: boolean;
        /** Encrypted zip entries cannot be read or extracted. */
        encrypted: boolean;
    }

    interface ReadOptions {
        /** Maximum bytes to decompress. Default and `0`: 16 MiB. */
        maxSize?: number;
    }

    interface ExtractOptions {
        /** Replace existing files. Default `true`. With `false` an existing file rejects the whole extraction. */
        overwrite?: boolean;
        /** Maximum total bytes to write. Default `0`: unlimited. */
        maxSize?: number;
        /** Return `false` to skip an entry. Called for every entry before anything is written. */
        filter?: (entry: Entry) => boolean;
        /** Called before each selected entry is written. Return `false` to cancel. */
        onProgress?: (entry: Entry, index: number, count: number) => boolean | void;
    }

    /** Opens a zip, tar, tar.gz or gzip file, detected by content. */
    function open(path: string): Handle;

    /** Releases the archive. Do not use `archive` afterwards. */
    function close(archive: Handle): void;

    /** Returns the detected format (`.tar.gz` is `'tar'`). */
    function type(archive: Handle): Format;

    /** Lists the entries. The first call on a `.tar.gz` decompresses it once to build the index. */
    function list(archive: Handle): Entry[];

    /**
     * Reads one entry into memory. `name` may be omitted for a `.gz` file.
     * Throws `TOO_LARGE` past `options.maxSize`.
     */
    function read(archive: Handle, name?: string, options?: ReadOptions): ArrayBuffer;

    /**
     * Writes the entries below `destination` (default: current directory)
     * and returns how many were written.
     */
    function extractAll(archive: Handle, destination?: string, options?: ExtractOptions): number;

    /** Opens, extracts and closes any supported archive. Returns how many entries were written. */
    function extract(path: string, destination?: string, options?: ExtractOptions): number;

    /** Alias of `extract`, kept for scripts written for the old API. */
    function untar(path: string, destination?: string, options?: ExtractOptions): number;

    /** Decompresses gzip data held in memory. */
    function gunzip(data: Bytes, options?: ReadOptions): ArrayBuffer;

    /** Compresses data in memory as gzip. `level` 0-9, default 6. */
    function gzip(data: Bytes, options?: { level?: number }): ArrayBuffer;

    /* --- Background jobs ------------------------------------------------ */

    /**
     * Opaque handle for work running on a worker thread. The worker opens its
     * own copy of the archive and never runs script code; the script calls
     * `poll()` (e.g. once per frame) to follow it. Dropping the handle cancels
     * the job.
     */
    interface Job<T> {
        readonly __brand: 'ArchiveJob';
        readonly __result?: T;
    }

    type JobState = 'running' | 'done' | 'failed' | 'cancelled';

    interface JobStatus<T> {
        state: JobState;
        /** Entry being processed, `""` when none. */
        entry: string;
        entriesDone: number;
        /** 0 until the archive has been indexed. */
        entriesTotal: number;
        bytesDone: number;
        /** Declared size of the selected entries. */
        bytesTotal: number;
        /** When `state` is `'done'`. The same value on every later poll. */
        result?: T;
        /** When `state` is `'failed'` or `'cancelled'`. */
        error?: Error;
    }

    interface AsyncExtractOptions {
        /** As in `ExtractOptions`. Default `true`. */
        overwrite?: boolean;
        /** As in `ExtractOptions`. Default `0`: unlimited. */
        maxSize?: number;
        /**
         * Entries to extract: exact names, or prefixes ending with `/` for a
         * whole directory. Default: every entry. (Callbacks such as `filter`
         * cannot run on the worker; this replaces them.)
         */
        include?: string[];
    }

    /**
     * Starts `extract()` on a worker thread. The result is the number of
     * entries written. Validation still happens before the first write.
     */
    function extractAsync(path: string, destination?: string, options?: AsyncExtractOptions): Job<number>;

    /** Starts `read()` on a worker thread. `name` may be omitted for a `.gz` file. */
    function readAsync(path: string, name?: string, options?: ReadOptions): Job<ArrayBuffer>;

    /** Returns the job's progress without blocking. */
    function poll<T>(job: Job<T>): JobStatus<T>;

    /**
     * Blocks until the job settles or `timeoutMs` passes (default: no limit),
     * letting other threads run meanwhile, then returns `poll(job)`.
     */
    function wait<T>(job: Job<T>, timeoutMs?: number): JobStatus<T>;

    /**
     * Asks the job to stop before its next block of work. Files already
     * written stay; the file being written is removed.
     */
    function cancel(job: Job<unknown>): void;
}


/* === Module: Color (color) === */
/**
 * Packs RGBA components into the 32-bit color format used by AthenaEnv.
 *
 * The component order in the returned value is `0xAABBGGRR`:
 * red occupies the least-significant byte and alpha the most-significant.
 * Component values are converted to unsigned 8-bit values.
 *
 * The default alpha used by `new()` is `0x80`, matching the PS2 GS default
 * convention. Color helpers are pure and return a new packed value.
 *
 * @example
 * ```js
 * let tint = Color.new(255, 128, 0, 255);
 * tint = Color.setA(tint, 192);
 * console.log(Color.getR(tint), Color.getA(tint));
 * ```
 */
declare namespace Color {
    /** Packed `0xAABBGGRR` color value. */
    type Value = number;

    /** Creates a packed color from red, green, blue and optional alpha. */
    function new(r: number, g: number, b: number, a?: number): Value;
    /** Reads the red component in the range 0..255. */
    function getR(color: Value): number;
    /** Reads the green component in the range 0..255. */
    function getG(color: Value): number;
    /** Reads the blue component in the range 0..255. */
    function getB(color: Value): number;
    /** Reads the alpha component in the range 0..255. */
    function getA(color: Value): number;
    /** Returns `color` with its red component replaced. */
    function setR(color: Value, value: number): Value;
    /** Returns `color` with its green component replaced. */
    function setG(color: Value, value: number): Value;
    /** Returns `color` with its blue component replaced. */
    function setB(color: Value, value: number): Value;
    /** Returns `color` with its alpha component replaced. */
    function setA(color: Value, value: number): Value;
}


/* === Module: Draw (draw) === */
/**
 * Immediate-mode 2D primitives rendered by the PS2 GS.
 *
 * Coordinates may be fractional and are interpreted in screen space.
 * Drawing is queued; call `Screen.flip()` to present the completed frame.
 * Colors are packed `Color.Value` values.
 */
declare namespace Draw {
    /** Draws a single point. */
    function point(x: number, y: number, color: Color.Value): void;

    /** Draws a solid-color line segment. */
    function line(x1: number, y1: number, x2: number, y2: number,
        color: Color.Value): void;

    /** Draws a solid-color triangle. */
    function triangle(x1: number, y1: number, x2: number, y2: number,
        x3: number, y3: number, color: Color.Value): void;

    /** Draws a Gouraud-shaded triangle with one color per vertex. */
    function triangleGouraud(
        x1: number, y1: number, color1: Color.Value,
        x2: number, y2: number, color2: Color.Value,
        x3: number, y3: number, color3: Color.Value
    ): void;

    /** Draws a solid-color quadrilateral as a GS triangle strip. */
    function quad(x1: number, y1: number, x2: number, y2: number,
        x3: number, y3: number, x4: number, y4: number,
        color: Color.Value): void;

    /** Draws a Gouraud-shaded quadrilateral with one color per vertex. */
    function quadGouraud(
        x1: number, y1: number, color1: Color.Value,
        x2: number, y2: number, color2: Color.Value,
        x3: number, y3: number, color3: Color.Value,
        x4: number, y4: number, color4: Color.Value
    ): void;

    /** Draws a solid-color axis-aligned rectangle. */
    function rect(x: number, y: number, width: number, height: number,
        color: Color.Value): void;

    /** Draws a circle outline or filled circle. */
    function circle(x: number, y: number, radius: number,
        color: Color.Value, filled?: boolean): void;
}


/* === Module: Font (font) === */
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


/* === Module: Gamepad (gamepad) === */
/**
 * Controller input for up to eight players, as a singleton.
 *
 * Supported controllers: DualShock 2 and other PS2 pads on both controller
 * ports, up to four per port through a multitap, and DualShock 3/4 over USB
 * (two) or Bluetooth (two, with a USB Bluetooth adapter).
 *
 * Only the two controller ports work out of the box. Multitap, USB and
 * Bluetooth each need an IOP driver that costs IOP memory, so they start
 * disabled; turn on the ones the program uses, preferably before the first
 * `update()`:
 * ```js
 * Gamepad.configure({ multitap: true, usb: true });
 * ```
 *
 * Players are logical: a controller that connects takes the lowest free
 * player and keeps it until it disconnects, whatever port or cable it uses.
 * Controllers already plugged in at start-up are assigned about half a
 * second after the first `update()`, in this order: port 1 slots A-D, port 2
 * slots A-D, USB, Bluetooth. Without multitaps, the pads on port 1 and port 2
 * therefore become players 0 and 1.
 *
 * Call `Gamepad.update()` once per frame. It polls every controller and
 * freezes a snapshot, so everything read from a `Player` during the frame is
 * cheap and consistent. `Gamepad.player(i)` always returns the same object.
 *
 * Analog values are normalized: sticks in [-1, 1], pressure and rumble
 * strength in [0, 1].
 *
 * Example:
 * ```js
 * const p1 = Gamepad.player(0);
 *
 * while (true) {
 *     Gamepad.update();
 *     if (p1.justDisconnected) pause();
 *     if (p1.justPressed(Gamepad.CROSS)) jump();
 *     const move = p1.leftStick();
 *     x += move.x * speed;
 *     if (hit) p1.rumble(0.8, 0, 200);
 *     Screen.flip();
 * }
 * ```
 */
declare namespace Gamepad {
    /** How the controller bound to a player is connected. */
    type Connection = "port" | "usb" | "bluetooth";

    /** State of one optional driver, see `Gamepad.drivers()`. */
    interface DriverState {
        /** Requested with `Gamepad.configure()`; all drivers start disabled. */
        readonly enabled: boolean;
        /** Loaded on the IOP and answering. */
        readonly ready: boolean;
    }

    /** One player. Obtain it with `Gamepad.player()`; it cannot be constructed. */
    interface Player {
        /** Player index, 0 to `MAX_PLAYERS - 1`. */
        readonly index: number;
        /** True while a controller is bound to this player. */
        readonly connected: boolean;
        /** True only on the update where a controller was bound to this player. */
        readonly justConnected: boolean;
        /** True only on the update where the controller went away. */
        readonly justDisconnected: boolean;
        /** How the controller is connected, or null when there is none. */
        readonly connection: Connection | null;
        /** Controller port (0 or 1) for `"port"` connections, otherwise -1. */
        readonly port: number;
        /** Multitap slot (0-3, 0 without a multitap) for `"port"` connections, otherwise -1. */
        readonly slot: number;
        /**
         * Kind of device, a `TYPE_*` value (`TYPE_NONE` when empty). A
         * DualShock 2 stays `TYPE_DUALSHOCK` in digital mode; see `analog`.
         */
        readonly type: DeviceType;
        /** True while the controller is in analog mode, i.e. its sticks are live. */
        readonly analog: boolean;
        /** Bitmask of the buttons held at the last update. */
        readonly buttons: number;
        /** Bitmask of the buttons held at the update before the last one. */
        readonly previousButtons: number;
        /** True when face, shoulder and d-pad buttons report real pressure (DualShock 2/3). */
        readonly hasPressure: boolean;
        /** True once the vibration motors are available. */
        readonly hasRumble: boolean;
        /**
         * Radial dead zone used by `leftStick()` and `rightStick()`, in
         * [0, 0.95]. Defaults to 0.15; set 0 for unfiltered values. Belongs to
         * the player, so it applies to whichever controller is bound.
         */
        deadzone: number;
        /** Same as `leftStick().x`, without allocating an object. */
        readonly leftX: number;
        /** Same as `leftStick().y`, without allocating an object. */
        readonly leftY: number;
        /** Same as `rightStick().x`, without allocating an object. */
        readonly rightX: number;
        /** Same as `rightStick().y`, without allocating an object. */
        readonly rightY: number;

        /** True when every button in `buttons` (e.g. `L1 | R1`) is held. */
        pressed(buttons: number): boolean;
        /** True on the update the `buttons` combination became fully held. */
        justPressed(buttons: number): boolean;
        /** True on the update the last held button of `buttons` was released. */
        justReleased(buttons: number): boolean;
        /** True when at least one button in `buttons` is held, e.g. any d-pad direction. */
        anyPressed(buttons: number): boolean;
        /** True on the update at least one button in `buttons` became held. */
        anyJustPressed(buttons: number): boolean;
        /**
         * Auto repeat for menus: true on the update a button in `buttons`
         * becomes held, then after `delayMs` (default 400) and every
         * `intervalMs` (default 100) while it stays held. Stateless, so it
         * can be called any number of times per frame.
         */
        repeatPressed(buttons: number, delayMs?: number, intervalMs?: number): boolean;
        /**
         * D-pad as a direction: each axis is -1, 0 or 1; y is negative upwards
         * like the sticks. Opposite directions held together cancel out.
         */
        dpad(): { x: -1 | 0 | 1; y: -1 | 0 | 1 };
        /**
         * Left stick in [-1, 1] with the dead zone applied; y is negative
         * upwards. Allocates an object per call; prefer `leftX`/`leftY` in
         * per-frame code for many players.
         */
        leftStick(): { x: number; y: number };
        /** Right stick in [-1, 1] with the dead zone applied; y is negative upwards. */
        rightStick(): { x: number; y: number };
        /**
         * How hard one button is pressed, in [0, 1]. Buttons without a sensor
         * report 1 while held. On a DualShock 4 only L2 and R2 are analog.
         */
        pressure(button: Button): number;
        /**
         * Vibrates the controller. `strong` drives the big motor and `weak`
         * the small one, both in [0, 1]; the small motor of the DualShock 2
         * and 3 only switches on (any `weak` above 0) or off. With
         * `durationMs` the motors stop by themselves, otherwise they run
         * until changed. Cleared when the controller disconnects; ignored
         * while the player has no controller.
         */
        rumble(strong: number, weak?: number, durationMs?: number): void;
        /** Stops both motors. */
        stopRumble(): void;
        /**
         * Requests analog (`true`, the default) or digital mode for PS2
         * controllers. When `lock` is true (default) the ANALOG button cannot
         * change it. Kept by the player and applied to every controller bound
         * to it. DualShock 3/4 are always analog.
         */
        setAnalog(enabled: boolean, lock?: boolean): void;
        /**
         * Stores the Bluetooth adapter's address in the DualShock 3/4 plugged
         * in over USB for this player, so it connects wirelessly once
         * unplugged. Needs the `usb` and `bluetooth` drivers.
         *
         * This **replaces the pairing saved in the controller**: a DualShock 3
         * paired with a PS3 stops connecting to it. It therefore requires an
         * explicit `{ overwrite: true }`; ask the user before calling it.
         *
         * Returns false when no Bluetooth adapter is present (see
         * `drivers().bluetooth.adapter`). Throws `TypeError` without the
         * confirmation or when the controller is not on USB. Blocks for a few
         * milliseconds.
         */
        pairBluetooth(options: { overwrite: true }): boolean;
        /**
         * Plain snapshot of the player (connection, type, buttons, sticks,
         * d-pad, capabilities), so `JSON.stringify(player)` and logging show
         * its state.
         */
        toJSON(): {
            index: number; connected: boolean; connection: Connection | null;
            port: number; slot: number; type: DeviceType; analog: boolean; buttons: number;
            leftStick: { x: number; y: number }; rightStick: { x: number; y: number };
            dpad: { x: number; y: number }; hasPressure: boolean; hasRumble: boolean;
            deadzone: number;
        };
    }

    /**
     * Polls every controller and captures this frame's snapshot. Call exactly
     * once per frame. The first call loads padman and the enabled drivers.
     * Throws `InternalError` when padman cannot be started; optional drivers
     * that fail are reported by `drivers()` instead.
     */
    function update(): void;
    /** Returns the persistent object of player `index` (0 to `MAX_PLAYERS - 1`). */
    function player(index: number): Player;
    /** All players, by index. The array cannot be modified. */
    const players: readonly Player[];
    /** Players with a controller bound, by index. */
    function connectedPlayers(): Player[];
    /**
     * First player whose `justPressed(buttons)` is true, or null. Useful for
     * "press START to join" screens.
     */
    function findJustPressed(buttons: number): Player | null;

    /**
     * Enables or disables optional drivers; omitted options keep their value.
     * All start disabled. An enabled driver is loaded on the next `update()`
     * and costs IOP memory from then on. Enable drivers before the first
     * update so the controllers on them join the start-up assignment order;
     * enabled later, they get players as they are found. Disabling a loaded
     * driver releases its controllers but does not unload it.
     */
    function configure(options: { multitap?: boolean; usb?: boolean; bluetooth?: boolean }): void;
    /**
     * Enabled and ready state of each optional driver. For Bluetooth,
     * `adapter` tells whether a USB Bluetooth adapter was found (one RPC; do
     * not call every frame).
     */
    function drivers(): {
        multitap: DriverState;
        usb: DriverState;
        bluetooth: DriverState & { readonly adapter: boolean };
    };
    /** True while a multitap is plugged into controller `port` (0 or 1). */
    function hasMultitap(port: number): boolean;
    /**
     * Exchanges the controllers of players `a` and `b`, with their buttons,
     * edges and rumble; either may be empty. Dead zone and analog preference
     * stay with each player and are applied to the controller it receives.
     * Use it to let whoever presses START first become player 0:
     * ```js
     * const who = Gamepad.findJustPressed(Gamepad.START);
     * if (who) Gamepad.swapPlayers(0, who.index);
     * ```
     */
    function swapPlayers(a: number, b: number): void;

    /** Number of players, and length of `players`. */
    const MAX_PLAYERS: 8;

    /*
     * Button bits. Combine them with `|` for the methods that take a mask,
     * e.g. `player.pressed(Gamepad.L1 | Gamepad.R1)`.
     */
    const SELECT: 0x0001;
    const L3: 0x0002;
    const R3: 0x0004;
    const START: 0x0008;
    const UP: 0x0010;
    const RIGHT: 0x0020;
    const DOWN: 0x0040;
    const LEFT: 0x0080;
    const L2: 0x0100;
    const R2: 0x0200;
    const L1: 0x0400;
    const R1: 0x0800;
    const TRIANGLE: 0x1000;
    const CIRCLE: 0x2000;
    const CROSS: 0x4000;
    const SQUARE: 0x8000;

    /** A single button, as taken by `pressure()`. */
    type Button = typeof SELECT | typeof L3 | typeof R3 | typeof START |
        typeof UP | typeof RIGHT | typeof DOWN | typeof LEFT |
        typeof L2 | typeof R2 | typeof L1 | typeof R1 |
        typeof TRIANGLE | typeof CIRCLE | typeof CROSS | typeof SQUARE;

    const TYPE_NONE: 0;
    const TYPE_NEJICON: 0x2;
    const TYPE_KONAMIGUN: 0x3;
    const TYPE_DIGITAL: 0x4;
    const TYPE_ANALOG: 0x5;
    const TYPE_NAMCOGUN: 0x6;
    const TYPE_DUALSHOCK: 0x7;
    const TYPE_JOGCON: 0xE;
    const TYPE_DUALSHOCK3: 0x1003;
    const TYPE_DUALSHOCK4: 0x1004;

    /** Value of `player.type`. */
    type DeviceType = typeof TYPE_NONE | typeof TYPE_NEJICON | typeof TYPE_KONAMIGUN |
        typeof TYPE_DIGITAL | typeof TYPE_ANALOG | typeof TYPE_NAMCOGUN |
        typeof TYPE_DUALSHOCK | typeof TYPE_JOGCON | typeof TYPE_DUALSHOCK3 |
        typeof TYPE_DUALSHOCK4;
}


/* === Module: Image (image) === */
/**
 * Image loading, CPU pixel access and textured 2D drawing.
 *
 * `Image` accepts paths understood by the active PS2 filesystem driver,
 * including paths relative to the boot directory. A newly loaded image is
 * CPU-resident; call `lock()` when it must remain resident in VRAM.
 *
 * Pixel buffers use the image's current `bpp` and dimensions. For 32-bit
 * images, `pixels` contains four bytes per pixel. Palette data is used only
 * by indexed 4-bit and 8-bit formats.
 *
 * @example
 * ```js
 * const logo = new Image('my_image.png');
 * if (!logo.ready()) throw new Error('image load failed');
 * logo.color = Color.new(255, 255, 255, 255);
 * logo.lock();
 * logo.draw(100, 80);
 * Screen.flip();
 * ```
 */

/** Optional destination, source-rectangle and tint overrides for `draw()`. */
type ImageDrawOptions = {
    /** Destination width in pixels; defaults to `width`. */
    width?: number;
    /** Destination height in pixels; defaults to `height`. */
    height?: number;
    /** Source rectangle's left coordinate in texture pixels. */
    startx?: number;
    /** Source rectangle's top coordinate in texture pixels. */
    starty?: number;
    /** Source rectangle's right coordinate in texture pixels. */
    endx?: number;
    /** Source rectangle's bottom coordinate in texture pixels. */
    endy?: number;
    /** Rotation angle in radians. */
    angle?: number;
    /** Packed RGBA tint, normally created with `Color.new()`. */
    color?: number;
};

/** Options controlling image creation and texture upload behavior. */
type ImageOptions = {
    /** Whether texture uploads use the deferred VIF1 path; defaults to true. */
    delayed?: boolean;
};

declare class Image {
    /** Loads an image from `path`, or creates an empty image when omitted. */
    constructor(options?: ImageOptions);
    constructor(path: string, options?: ImageOptions);
    /** Linear size in bytes of the current pixel buffer. */
    readonly size: number;
    /** Whether texture uploads use the deferred VIF1 path. */
    readonly delayed: boolean;
    /** CPU pixel buffer; assigning it copies the supplied `ArrayBuffer`. */
    pixels: ArrayBuffer;
    /** CPU palette buffer for indexed images; required for indexed images. */
    palette: ArrayBuffer;
    /** Texture width in pixels (1..1024); changing it discards pixels and VRAM. */
    texWidth: number;
    /** Texture height in pixels (1..1024); changing it discards pixels and VRAM. */
    texHeight: number;
    /** Pixel storage format: 4, 8, 16, 24 or 32 bits per pixel; changing it discards storage. */
    bpp: number;
    /** Texture filter mode; must be GS_FILTER_NEAREST or GS_FILTER_LINEAR. */
    filter: number;
    /** Whether dimensions and a valid pixel buffer are available for drawing. */
    renderable: boolean;
    /** Destination draw width in pixels. */
    width: number;
    /** Destination draw height in pixels. */
    height: number;
    /** Source rectangle's left coordinate in texture pixels. */
    startx: number;
    /** Source rectangle's top coordinate in texture pixels. */
    starty: number;
    /** Source rectangle's right coordinate in texture pixels. */
    endx: number;
    /** Source rectangle's bottom coordinate in texture pixels. */
    endy: number;
    /** Rotation angle in radians used by `draw()`. */
    angle: number;
    /** Packed RGBA tint multiplied with sampled texture color. */
    color: number;

    /** True when dimensions, pixel data and indexed palette data are valid. */
    ready(): boolean;
    /** True while an ImageList request is waiting or being processed. */
    loading(): boolean;
    /** True when the most recent ImageList request failed. */
    failed(): boolean;
    /**
     * Returns the loading state. `decoded` has CPU pixels; `upload_pending`
     * has a queued VRAM upload; `ready` is resident in VRAM.
     */
    status(): "queued" | "loading" | "decoded" | "upload_pending" | "ready" | "failed" | "cancelled";
    /** Returns structured load diagnostics, or undefined when no load failed. */
    error(): ImageLoadError | undefined;
    /** Queues a textured sprite at `(x, y)` for the current frame. */
    draw(x: number, y: number, options?: ImageDrawOptions): void;
    /** Uploads the image synchronously and pins its VRAM allocation. */
    lock(): boolean;
    /** Allows the texture manager to evict the image from VRAM. */
    unlock(): boolean;
    /** Returns whether the image is currently pinned in VRAM. */
    locked(): boolean;
    /** Converts an unlocked CT24 texture to CT16S and invalidates its VRAM copy. */
    optimize(): boolean;
    /** Releases the native image and its CPU/VRAM resources. */
    free(): void;

    /** Copies a rectangular VRAM region between two resident images. */
    static copyVRAMBlock(
        source: Image,
        sourceX: number,
        sourceY: number,
        destination: Image,
        destinationX: number,
        destinationY: number
    ): void;
}

/** Structured diagnostics for a failed image load. */
interface ImageLoadError {
    /** Path as it was requested. */
    path: string;
    code: "open_failed" | "unsupported_format" | "decode_failed" | "surface_failed" | "upload_failed";
    /** `upload` is reported only for ImageList requests with an `upload` option. */
    stage: "open" | "decode" | "surface" | "upload";
    /** Human-readable description. */
    message: string;
}


/* === Module: ImageList (imagelist) === */
/**
 * Cooperative asynchronous image loading.
 *
 * ImageList applies decoded images to surfaces and VRAM, and runs callbacks,
 * only on the thread that calls `process()`. By default decoding also happens
 * there; `new ImageList({ workers: 1 })` moves file I/O and decoding to one
 * CPU worker thread. Call `process()` from the frame loop with a small budget
 * to bound the work performed in one frame.
 *
 * @example
 * ```js
 * const images = new ImageList();
 * const logo = images.load("tests/my_image.png", {
 *     onLoad: (image) => image.lock(),
 *     onError: (image, error) => console.log(`Failed: ${error.path}`),
 * });
 *
 * while (true) {
 *     images.process(1);
 *     Screen.clear(0x80182030);
 *     if (logo.ready()) logo.draw(100, 80);
 *     Screen.flip();
 * }
 * ```
 */

/**
 * Options for one queued image request.
 *
 * Callbacks run synchronously inside `process()` on the same thread that
 * called it. The returned `Image` remains valid after the callback and is
 * owned by the caller.
 */
interface ImageListLoadOptions {
    /** Whether texture uploads use the deferred VIF1 path; defaults to true. */
    delayed?: boolean;
    /**
     * Queue priority; defaults to `ImageList.NORMAL`. Requests with the same
     * priority keep their submission order.
     */
    priority?: number;
    /**
     * When the texture becomes resident in VRAM:
     * - `"draw"` (default): on the first draw, as with any `Image`.
     * - `"bind"`: inside `process()`, before `onLoad`. The texture manager may
     *   still evict it later.
     * - `"lock"`: inside `process()`, and locked until `unlock()`.
     *
     * With `"bind"` or `"lock"`, a texture that does not fit in VRAM fails
     * with `stage: "upload"`; its pixels are released and `onError` runs. The
     * status is `"ready"` when `onLoad` runs. Duplicate requests use the
     * strongest mode asked for.
     */
    upload?: "draw" | "bind" | "lock";
    /** Called after the image has been decoded successfully. */
    onLoad?: (image: Image) => void;
    /** Called after loading fails with structured diagnostics. */
    onError?: (image: Image, error: ImageLoadError) => void;
}

/** Options for an `ImageList` queue. */
interface ImageListOptions {
    /**
     * `1` decodes files on a single CPU worker thread; `0` (the default)
     * decodes inside `process()`. In both modes surfaces, VRAM and callbacks
     * are handled only on the thread that calls `process()`.
     */
    workers?: 0 | 1;
    /**
     * Worker mode only: decoded bytes the worker may keep ready ahead of
     * `process()`. `0` (the default) keeps at most one decoded image waiting.
     * The peak is bounded by this limit plus one decoded image.
     */
    maxMemory?: number;
    /**
     * Number of successfully loaded images kept for reuse, least recently
     * used first out; `0` (the default) disables the cache. Loading a cached
     * path returns the same `Image` without decoding it again, and its
     * callbacks still run inside `process()`. The cache holds a reference to
     * each `Image` but never locks it, so VRAM residency is unaffected.
     * Entries leave on eviction, `clearCache()`, `Image.free()` or when the
     * list is destroyed. At most 1024.
     */
    cacheSize?: number;
}

interface ImageListProcessOptions {
    /** Maximum number of requests to complete; defaults to one. */
    maxItems?: number;
    /**
     * Stops after the decoded bytes completed in this call reach this value.
     * The first request always completes; zero means no byte limit.
     */
    maxBytes?: number;
    /**
     * Stops starting new requests once this many milliseconds have elapsed
     * in this call. The first request always completes, so one large image
     * can exceed the limit; zero means no time limit.
     */
    maxTime?: number;
}

interface ImageListStats {
    /** Requests waiting to be decoded. */
    queued: number;
    /** Requests being decoded by the worker or waiting to be applied. */
    loading: number;
    completed: number;
    failed: number;
    cancelled: number;
    /** Decoded bytes currently held by the worker and not yet applied. */
    bufferedBytes: number;
    /** Largest decoded-but-unapplied CPU memory observed. */
    peakBufferedBytes: number;
    /** Worker threads currently running: 0 or 1. */
    workers: number;
    /** Images currently held by the cache. */
    cached: number;
    /** Requests completed from the cache; also counted in `completed`. */
    cacheHits: number;
    /** Total milliseconds spent decoding, on the worker or in `process()`. */
    decodeTime: number;
    /** Total milliseconds spent building surfaces from decoded buffers. */
    applyTime: number;
    /** Total milliseconds spent on `upload: "bind"` / `"lock"` VRAM uploads. */
    uploadTime: number;
}

declare class ImageList {
    static readonly HIGH: number;
    static readonly NORMAL: number;
    static readonly LOW: number;

    /** Creates an empty request queue. */
    constructor(options?: ImageListOptions);

    /**
     * Queues an image and returns it immediately in the queued state.
     *
     * Requests for the same normalized path that are still pending share one
     * decode and return the same `Image`; every caller's callbacks run. A more
     * urgent duplicate promotes a request that has not started loading. The
     * returned image can be inspected with `status()`, `loading()`, `ready()`
     * and `failed()` while it is pending.
     */
    load(path: string, options?: ImageListLoadOptions): Image;
    /**
     * Completes up to `budget` requests and dispatches their callbacks.
     *
     * A zero budget performs no work. The default budget is one image.
     * Returns the number of requests completed, including failures. In worker
     * mode only requests the worker has already decoded are completed, so
     * this can return zero while requests are still loading. Cache hits
     * complete before decoded requests and count toward the budget.
     */
    process(budget?: number | ImageListProcessOptions): number;
    /** Returns the number of requests not completed yet (queued or loading). */
    pending(): number;
    /** Returns queue, completion and memory counters. */
    stats(): ImageListStats;
    /**
     * Cancels one pending request. Returns true when a request was
     * cancelled, or false when it was already completed or not owned by
     * this list. A request the worker is decoding is discarded when the
     * decode finishes; its callbacks never run.
     */
    cancel(image: Image): boolean;
    /**
     * Cancels all pending requests.
     *
     * Already returned `Image` objects are not destroyed. Their pending
     * callbacks are discarded and their `status()` becomes "cancelled".
     * A request served from the cache only loses its callbacks; its image
     * keeps its current status.
     */
    clear(): void;
    /** Releases every cached image reference and returns how many were held. */
    clearCache(): number;
    /**
     * Releases the list's resources now instead of waiting for the garbage
     * collector. Pending requests are cancelled as by `clear()`. The worker
     * thread is joined and its stack freed, and the cache is emptied.
     * Afterwards `load()` throws a TypeError. `process()` returns 0, while
     * `stats()` keeps working. Calling it again does nothing.
     */
    close(): void;
}


/* === Module: IOP (iop) === */
/**
 * IOP module and module-manager bindings.
 *
 * IOP module identifiers are numeric registry IDs. A module can be queried
 * by either its name or ID. Loading a module may start its dependencies.
 *
 * Example:
 * ```js
 * const fileXio = IOP.getModule('fileXio');
 * if (fileXio && !fileXio.started) IOP.loadModule(fileXio.id);
 * console.log(IOP.getMemoryStats().free);
 * ```
 */
declare namespace IOP {
    /** Metadata for one module registered with the IOP manager. */
    interface Module {
        /** Numeric registry identifier. */
        id: number;
        /** Registered module name. */
        name: string;
        /** True after the module has initialized successfully. */
        started: boolean;
        /** True when the module is part of the boot-time set. */
        startAtBoot: boolean;
    }

    /** Snapshot of IOP memory usage in bytes. */
    interface MemoryStats {
        /** Available IOP RAM. */
        free: number;
        /** RAM currently in use by IOP modules. */
        used: number;
    }

    /** Returns a snapshot of all registered IOP modules. */
    function getModules(): Module[];
    /** Resolves a module by its name or numeric registry ID. */
    function getModule(nameOrId: string | number): Module;
    /** Loads and initializes a module and its dependencies. */
    function loadModule(nameOrId: string | number): number;
    /** Resets the IOP and reinstalls the registered boot modules. */
    function reset(): void;
    /** Returns free and used IOP RAM in bytes. */
    function getMemoryStats(): MemoryStats;
}


/* === Module: Vector (vector) === */
/**
 * PS2-aligned vector types.
 *
 * `Vector2`, `Vector3` and `Vector4` are separate JavaScript classes exposed
 * by the `Vector` module. Arithmetic methods return new vectors and do not
 * mutate their operands. `div()` rejects zero components.
 *
 * Example:
 * ```js
 * import * as Vector from 'Vector';
 * const direction = new Vector.Vector3(3, 4, 0);
 * console.log(direction.norm());
 * const right = direction.cross(new Vector.Vector3(0, 0, 1));
 * ```
 */
declare class Vector2 {
    /** Creates a two-component vector. */
    constructor(x: number, y: number);
    /** Horizontal component. */
    x: number;
    /** Vertical component. */
    y: number;
    /** Returns Euclidean length. */
    norm(): number;
    /** Returns the dot product. */
    dot(value: Vector2): number;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector2): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector2): number;
    /** Returns the component-wise sum. */
    add(value: Vector2): Vector2;
    /** Returns the component-wise difference. */
    sub(value: Vector2): Vector2;
    /** Returns the component-wise product. */
    mul(value: Vector2): Vector2;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector2): Vector2;
    /** Returns a readable component representation. */
    toString(): string;
}

declare class Vector3 {
    /** Creates a three-component vector. */
    constructor(x: number, y: number, z: number);
    /** X component. */
    x: number;
    /** Y component. */
    y: number;
    /** Z component. */
    z: number;
    /** Returns Euclidean length. */
    norm(): number;
    /** Returns the dot product. */
    dot(value: Vector3): number;
    /** Returns the 3D cross product. */
    cross(value: Vector3): Vector3;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector3): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector3): number;
    /** Returns the component-wise sum. */
    add(value: Vector3): Vector3;
    /** Returns the component-wise difference. */
    sub(value: Vector3): Vector3;
    /** Returns the component-wise product. */
    mul(value: Vector3): Vector3;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector3): Vector3;
    /** Returns a readable component representation. */
    toString(): string;
}

declare class Vector4 {
    /** Creates a homogeneous four-component vector. */
    constructor(x: number, y: number, z: number, w: number);
    /** X component. */
    x: number;
    /** Y component. */
    y: number;
    /** Z component. */
    z: number;
    /** Homogeneous component: commonly 1 for points and 0 for directions. */
    w: number;
    /** Returns four-dimensional Euclidean length. */
    norm(): number;
    /** Returns the four-component dot product. */
    dot(value: Vector4): number;
    /** Returns the cross product with homogeneous component cleared. */
    cross(value: Vector4): Vector4;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector4): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector4): number;
    /** Returns the component-wise sum. */
    add(value: Vector4): Vector4;
    /** Returns the component-wise difference. */
    sub(value: Vector4): Vector4;
    /** Returns the component-wise product. */
    mul(value: Vector4): Vector4;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector4): Vector4;
    /** Returns a readable component representation. */
    toString(): string;
}


/* === Module: Matrix4 (matrix4) === */
/**
 * Four-by-four transformation matrix using the PS2/AthenaEnv layout.
 *
 * Values are stored in column-major order. Translation components are at
 * indices 12, 13 and 14; index 15 is the homogeneous component.
 *
 * Example:
 * ```js
 * const transform = new Matrix4();
 * transform.set(12, 10).set(13, 20).set(14, 30);
 * const inverse = transform.clone().invert();
 * console.log(inverse.get(12), inverse.get(13), inverse.get(14));
 * ```
 */
declare class Matrix4 {
    /** Creates identity matrix, or initializes all 16 values when supplied. */
    constructor();
    constructor(
        m00: number, m01: number, m02: number, m03: number,
        m10: number, m11: number, m12: number, m13: number,
        m20: number, m21: number, m22: number, m23: number,
        m30: number, m31: number, m32: number, m33: number
    );
    /** Number of scalar components in the matrix. */
    readonly length: 16;
    /** Reads a scalar component at index 0..15. */
    get(index: number): number;
    /** Writes a scalar component at index 0..15 and returns this matrix. */
    set(index: number, value: number): this;
    /** Compares all 16 components exactly. */
    equals(value: Matrix4): boolean;
    /** Compares all components using an absolute epsilon tolerance. */
    equalsEpsilon(value: Matrix4, epsilon: number): boolean;
    /** Returns the 16 components as a new array. */
    toArray(): number[];
    /** Copies 16 values from an array-like object into this matrix. */
    fromArray(values: ArrayLike<number>): this;
    /** Returns an independent copy of this matrix. */
    clone(): Matrix4;
    /** Copies another matrix into this matrix. */
    copy(value: Matrix4): this;
    /** Returns the product of this matrix and `value`. */
    multiply(value: Matrix4): Matrix4;
    /** Replaces this matrix with identity. */
    identity(): this;
    /** Transposes this matrix in place. */
    transpose(): this;
    /** Inverts this matrix in place; throws for a singular matrix. */
    invert(): this;
    /** Returns a readable 16-value representation. */
    toString(): string;
}


/* === Module: Screen (screen) === */
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
        /** Reserved for future multi-pass rendering; only zero is currently accepted. */
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
    /** Returns the total or used VRAM amount for the selected `VRAM_*` accounting mode. */
    function getMemoryStats(mode?: number): number;
    /** Returns currently unallocated VRAM in bytes. */
    function getFreeVRAM(): number;
    /** Returns the measured FPS over the requested positive frame interval. */
    function getFPS(interval: number): number;
    /** Returns the active video configuration. */
    function getMode(): VideoMode;
    /** Reconfigures the video mode and render targets; invalid modes throw. */
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
    /** Sends queued graphics commands without waiting for DMA, VIF/GIF completion, or VBlank. */
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


/* === Module: System Core (system) === */
/**
 * PS2 system, filesystem, timing and hardware helpers.
 *
 * Paths use the PS2 device syntax such as `host:/`, `mass:/` or `mc0:/`.
 * Return values from filesystem and device operations are native result codes;
 * callers should check them before continuing.
 *
 * Example:
 * ```js
 * console.log(System.bootPath);
 * for (const entry of System.listDir('host:/')) {
 *     console.log(entry.dir ? '[DIR]' : entry.size, entry.name);
 * }
 * System.sleep(16);
 * ```
 */
declare namespace System {
    /** One directory entry returned by `listDir()`. */
    interface DirectoryEntry {
        /** File or directory name. */
        name: string;
        /** File size in bytes; directory sizes may be zero. */
        size: number;
        /** True when this entry is a directory. */
        dir: boolean;
    }

    /** Memory counters returned by `getMemoryStats()`. */
    interface MemoryStats {
        /** Core/binary footprint in bytes. */
        core: number;
        /** Reserved native stack in bytes. */
        nativeStack: number;
        /** Current native allocations in bytes. */
        allocs: number;
        /** Total reported usage in bytes. */
        used: number;
    }

    /** EE CPU information returned by `getCPUInfo()`. */
    interface CPUInfo {
        /** EE CPU implementation identifier. */
        implementation: number;
        /** EE CPU revision identifier. */
        revision: number;
        /** Installed EE RAM size in bytes. */
        RAMSize: number;
        /** EE bus clock frequency. */
        BUSClock: number;
        /** EE CPU clock frequency. */
        CPUClock: number;
        /** PS2 machine type identifier. */
        MachineType: number;
    }

    /** Memory-card status returned by `getMCInfo()`. */
    interface MemoryCardInfo {
        /** Memory-card type identifier. */
        type: number;
        /** Free memory reported by the card driver. */
        freemem: number;
        /** Format/status flag reported by the card driver. */
        format: number;
    }

    /** GS GPU information returned by `getGPUInfo()`. */
    interface GPUInfo {
        revision: number;
        id: number;
    }

    /** One registered filesystem/device entry. */
    interface DeviceInfo {
        name: string;
        desc: string;
    }

    /** The path from which the application booted (e.g. "mass0:/", "cdfs:/") */
    const bootPath: string;
    /** Legacy alias for bootPath. */
    const boot_path: string;

    /** Lists entries in a directory or path relative to `bootPath`. */
    function listDir(path?: string): DirectoryEntry[];

    /** Removes an empty directory and returns the underlying system result. */
    function removeDirectory(path: string): number;

    /** Copies a file and returns zero on success. */
    function copyFile(source: string, destination: string): number;

    /** Moves or renames a file and returns zero on success. */
    function moveFile(source: string, destination: string): number;
    /** Renames a file or directory and returns the native result code. */
    function rename(source: string, destination: string): number;

    /** Returns raw EE CPU clock ticks. */
    function getTicks(): number;

    /** Returns high-resolution elapsed time in milliseconds. */
    function getMilliseconds(): number;

    /** Suspends the current EE thread for the specified milliseconds. */
    function sleep(ms: number): void;

    /** Returns currently used EE RAM in bytes. */
    function getUsedMemory(): number;

    /** Returns remaining available EE RAM in bytes. */
    function getFreeMemory(): number;

    /** Yields briefly to the EE scheduler. */
    function delay(): void;

    /** Returns memory counters from the legacy System API. */
    function getMemoryStats(): MemoryStats;

    /** Returns basic EE CPU and memory information. */
    function getCPUInfo(): CPUInfo;

    /** Returns basic GS GPU information. */
    function getGPUInfo(): GPUInfo;

    /** Returns the console temperature in Celsius when supported. */
    function getTemperature(): number | undefined;

    /** Returns memory-card information for a controller port (0 or 1). */
    function getMCInfo(port?: number): MemoryCardInfo;

    /** Returns information about a mass-storage block device. */
    function getBDMInfo(device: string): { name: string; index: number } | undefined;

    /** Returns currently registered file-system devices. */
    function devices(): DeviceInfo[];

    /** Mounts a block device at a file-system mount point. */
    function mount(mountpoint: string, blockdev: string, mode?: number): number;

    /** Unmounts a file-system device. */
    function umount(device: string): number;

    /** Loads an ELF using the legacy Athena loader. */
    function loadELF(path: string, args?: string[]): number;

    /** Enables or disables the legacy dark-mode flag. */
    function setDarkMode(enabled: boolean): void;

    /** Forces a QuickJS garbage-collection cycle. */
    function gc(): void;

    /** Exit application to the PS2 browser/OSDSYS */
    function exit(): void;

    /** Alias for exiting to the PS2 browser/OSDSYS. */
    function exitToBrowser(): void;
}


/* === Module: TileMap (tilemap) === */
/**
 * VU1-accelerated batched sprite and tilemap rendering.
 *
 * A `Descriptor` holds textures and materials; an `Instance` pairs one with a
 * native sprite buffer. Sprites are streamed to a VU1 microprogram in
 * batches, so thousands of quads cost little EE time. Draws are queued like
 * any other drawing; call `Screen.flip()` to present them.
 *
 * @example
 * ```js
 * const descriptor = new TileMap.Descriptor({
 *     textures: ["tiles.png"],
 *     materials: [{ textureIndex: 0, endOffset: 1 }],
 * });
 * const map = new TileMap.Instance({
 *     descriptor,
 *     spriteBuffer: TileMap.SpriteBuffer.fromObjects([
 *         { x: 0, y: 0, w: 32, h: 32, u2: 32, v2: 32 },
 *         { x: 32, y: 0, w: 32, h: 32, u1: 32, u2: 64, v2: 32 },
 *     ]),
 * });
 *
 * while (true) {
 *     Screen.clear();
 *     map.render(0, 0);
 *     Screen.flip();
 * }
 * ```
 */
declare namespace TileMap {
    /**
     * One draw state for a contiguous run of sprites. Material `i` draws the
     * sprites after material `i - 1`'s `endOffset` up to and including its
     * own `endOffset`.
     */
    interface Material {
        /**
         * Index into the descriptor's textures, or -1 for untextured
         * sprites. Defaults to 0 when the descriptor has textures, otherwise
         * -1.
         */
        textureIndex?: number;
        /**
         * Alpha blend equation from `Screen.alphaEquation()`. Defaults to
         * the current `Screen` equation at render time.
         */
        blendMode?: number;
        /** Index of the last sprite this material draws. Must not decrease. */
        endOffset: number;
    }

    /**
     * Tileset geometry. Tile `id` is the cell at column `id % columns`, row
     * `Math.floor(id / columns)` of the atlas texture.
     */
    interface Atlas {
        tileWidth: number;
        tileHeight: number;
        columns: number;
        /** When set, tile ids must be below `columns * rows`. */
        rows?: number;
    }

    interface DescriptorOptions {
        /** Required by `Instance.fromGrid()` and `Instance.setTiles()`. */
        atlas?: Atlas;
        /**
         * Textures by path or `Image`. Paths are loaded synchronously. An
         * `Image` that is still loading (for example from an `ImageList`) or
         * was freed skips the sprites of its materials until it is ready.
         */
        textures?: Array<string | Image>;
        /** At least one material, ordered by `endOffset`. */
        materials: Material[];
    }

    /** Immutable render description shared by any number of instances. */
    class Descriptor {
        constructor(options: DescriptorOptions);
        readonly materialCount: number;
        /** The `Image` objects the descriptor keeps alive. */
        readonly textures: Image[];
        readonly atlas: Atlas | undefined;
    }

    /** Tile id that hides a cell: `setTiles`/`fromGrid` give it zero size. */
    const EMPTY: number;

    /** Tile ids: a `Uint16Array` is used without copying. */
    type TileIds = Uint16Array | Int16Array | number[];

    interface GridOptions {
        /** Descriptor with an `atlas`. */
        descriptor: Descriptor;
        columns: number;
        rows: number;
        /** Row-major tile ids, `columns * rows` long; default all 0. */
        tiles?: TileIds;
        /** Cell size on screen; defaults to the atlas tile size. */
        tileWidth?: number;
        tileHeight?: number;
        zindex?: number;
    }

    interface RenderOptions {
        /** Draw only sprites [first, first + count). Disables culling. */
        first?: number;
        count?: number;
        /**
         * Grid instances draw only the cells on screen (plus one cell of
         * margin) by default; `false` draws every cell.
         */
        cull?: boolean;
    }

    /**
     * Native sprite storage. Buffers that are rendered must be 16-byte
     * aligned; `SpriteBuffer.create()` and `fromObjects()` always are. A
     * plain `new ArrayBuffer()` may not be.
     */
    type SpriteStorage = ArrayBuffer | ArrayBufferView;

    interface InstanceOptions {
        descriptor: Descriptor;
        /** Sprite records laid out as described by `TileMap.layout`. */
        spriteBuffer?: SpriteStorage;
    }

    /** A descriptor plus a sprite buffer that can be rendered. */
    class Instance {
        constructor(options: InstanceOptions);
        /**
         * Builds a row-major grid in native code: cell (column, row) is
         * sprite `row * columns + column` at (column * tileWidth,
         * row * tileHeight). Grid instances cull to the screen in
         * `render()`. Culling assumes cells stay near their position: a
         * sprite moved more than one cell away may be skipped.
         */
        static fromGrid(options: GridOptions): Instance;
        readonly descriptor: Descriptor;
        /** Sprites in the current buffer, or 0 without a buffer. */
        readonly spriteCount: number;
        /** Sprites queued by the last `render()`, after culling. */
        readonly lastDrawCount: number;
        /** Grid geometry for `fromGrid()` instances, else undefined. */
        readonly grid: { columns: number; rows: number;
            tileWidth: number; tileHeight: number } | undefined;
        /**
         * Queues sprites at (x, y) plus the camera offset. Sprites are read
         * when the frame is sent, so writes made to the buffer after
         * `render()` and before `Screen.flip()` may or may not be shown this
         * frame.
         */
        render(x: number, y: number, options?: RenderOptions): void;
        /** Moves sprites [first, first + count) in native code. */
        translate(first: number, count: number, dx: number, dy: number): void;
        /** Sets the color (0-255, 128 = neutral) of a sprite range. */
        setColor(first: number, count: number, r: number, g: number,
            b: number, a?: number): void;
        /**
         * Points sprites from `first` at atlas tiles, one per id, and sets
         * their size to the cell (grid) or atlas tile size; `EMPTY` hides a
         * sprite. All ids are validated before anything is written.
         */
        setTiles(first: number, tiles: TileIds): void;
        /**
         * Uses another buffer from now on. Waits for queued draws that read
         * the previous one, so replacing buffers mid-frame stalls briefly.
         */
        replaceSpriteBuffer(buffer: SpriteStorage): void;
        /** Returns the current buffer; edits through a `DataView` are live. */
        getSpriteBuffer(): SpriteStorage | undefined;
        /**
         * Copies `count` sprites (default: all of `source`) into the buffer
         * starting at sprite `dstOffset`. `source` needs no alignment.
         */
        updateSprites(dstOffset: number, source: SpriteStorage,
            count?: number): void;
    }

    /** Fields accepted by `SpriteBuffer.fromObjects()`. */
    interface SpriteObject {
        x?: number;
        y?: number;
        w?: number;
        h?: number;
        /** Texture coordinates in texels. */
        u1?: number;
        v1?: number;
        u2?: number;
        v2?: number;
        /**
         * Depth. The VU program passes the raw bits of this float to the
         * GS, so it orders correctly only for non-negative values and a
         * 24- or 32-bit Z buffer; with the default 16-bit Z buffer, depth
         * testing tiles is unreliable.
         */
        zindex?: number;
        /** Color channels 0-255; default 128 (0x80, neutral modulation). */
        r?: number;
        g?: number;
        b?: number;
        a?: number;
    }

    namespace SpriteBuffer {
        /** Allocates `count` zeroed sprites. */
        function create(count: number): ArrayBuffer;
        /** Builds a buffer from objects; missing fields are 0 (colors 128). */
        function fromObjects(sprites: SpriteObject[]): ArrayBuffer;
    }

    /** Byte layout of one sprite record, for `DataView` access. */
    const layout: {
        readonly stride: number;
        readonly offsets: {
            readonly x: number;
            readonly y: number;
            readonly w: number;
            readonly h: number;
            readonly u1: number;
            readonly v1: number;
            readonly u2: number;
            readonly v2: number;
            /** Colors are 32-bit unsigned integers. */
            readonly r: number;
            readonly g: number;
            readonly b: number;
            readonly a: number;
            readonly zindex: number;
        };
    };

    /**
     * Hardware debugging switches, for bisecting problems that appear only
     * on a real console. They slow rendering; leave them off otherwise.
     */
    interface Diagnostics {
        /** FLUSHA and TEX0/TEX1 before every batch, as the old renderer. */
        flushEachBatch: boolean;
        /** Write back the whole data cache instead of the sprite range. */
        fullCacheFlush: boolean;
        /** Sprites per VU1 batch, 1-50 (default 50). */
        batchSize: number;
    }

    /** Changes the given switches; the others keep their current value. */
    function setDiagnostics(options: Partial<Diagnostics>): void;
    function getDiagnostics(): Diagnostics;

    /** Offsets every instance drawn afterwards; defaults to (0, 0). */
    function setCamera(x: number, y: number): void;
    function getCamera(): { x: number; y: number };
}


/* === Module: Timer (timer) === */
/**
 * Manual native timer objects.
 *
 * Timer values are represented in the module's native clock-tick units.
 * These timers are distinct from the global event-loop functions such as
 * `setTimeout`.
 *
 * Example:
 * ```js
 * const timer = Timer.new();
 * Timer.pause(timer);
 * Timer.setTime(timer, 0);
 * Timer.resume(timer);
 * console.log(Timer.isPlaying(timer));
 * Timer.destroy(timer);
 * ```
 */
declare namespace Timer {
    /** Opaque handle returned by `Timer.new()`. */
    interface Handle {
        readonly __brand: 'Timer';
    }

    /** Creates a running timer. */
    function new(): Handle;

    /** Returns elapsed native clock ticks, frozen while paused. */
    function getTime(timer: Handle): number;

    /** Replaces the elapsed time in native clock ticks. */
    function setTime(timer: Handle, value: number): void;

    /** Pauses without resetting the elapsed time. */
    function pause(timer: Handle): void;

    /** Resumes a paused timer. */
    function resume(timer: Handle): void;

    /** Sets elapsed time to zero while preserving the timer object. */
    function reset(timer: Handle): void;

    /** Returns true when the timer is actively advancing. */
    function isPlaying(timer: Handle): boolean;

    /** Releases the native timer. Do not use `timer` afterwards. */
    function destroy(timer: Handle): void;
}
