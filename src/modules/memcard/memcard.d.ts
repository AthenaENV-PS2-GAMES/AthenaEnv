/**
 * Memory Card access on mc0: (port 0) and mc1: (port 1).
 *
 * Paths always name the card: `"mc0:/SAVEDATA/slot1.dat"`. Names are 1-31
 * bytes without `*` or `?`; `.` and `..` are resolved. Directories must
 * exist unless `createDirs` is used.
 *
 * Every call that talks to the card blocks the calling script thread (not
 * the others) until the card answers; saves take tens to hundreds of
 * milliseconds. In a frame loop use the `*Async` variants, which run on a
 * worker thread: poll them once per frame, or simply `await` them.
 *
 * Failures throw an `Error` with a stable `error.code` (see `ErrorCode`) and,
 * for a path, `error.path`.
 *
 * Example:
 * ```js
 * const card = MemoryCard.getInfo(0);
 * if (!card.connected) throw new Error("Insert a memory card in slot 1");
 *
 * // Save: the directory is created on the first save.
 * MemoryCard.writeJSON("mc0:/MYGAME/save.json", state, { atomic: true });
 * MemoryCard.writeFile("mc0:/MYGAME/icon.sys",
 *     MemoryCard.createIconSys({ title: "My Game\nSlot 1", icon: "icon.ico" }));
 *
 * // Load.
 * if (MemoryCard.exists("mc0:/MYGAME/save.json"))
 *     state = MemoryCard.readJSON("mc0:/MYGAME/save.json");
 *
 * // Without freezing the frame loop.
 * const job = MemoryCard.writeJSONAsync("mc0:/MYGAME/save.json", state);
 * Loop.run(() => {
 *     const s = MemoryCard.poll(job);
 *     if (s.state === "running") drawSpinner(s.bytesDone / s.bytesTotal);
 * });
 * // ...or, in an async function:
 * state = await MemoryCard.readJSONAsync("mc0:/MYGAME/save.json");
 * ```
 *
 * Limits of the driver:
 * - three files can be open at once for both cards together, `fopen("mc0:...")` included;
 * - `rename` only renames in place;
 * - each directory holds a fixed number of entries (`getFreeEntries`);
 * - raw page/block access is not available with the embedded XMCSERV driver.
 */
declare namespace MemoryCard {
    type Port = 0 | 1;

    /** `"mc0:/DIR/NAME"` or `"mc1:/DIR/NAME"`; `"mc0:/"` is the root. */
    type Path = string;

    type CardType = 'none' | 'ps1' | 'ps2' | 'pocketstation';

    type ErrorCode =
        | 'INVALID_ARGUMENT'
        /** The mcserv driver is not running (it is started on demand when possible). */
        | 'NOT_READY'
        /** No card in the slot, or it failed detection. */
        | 'NO_CARD'
        | 'UNFORMATTED'
        /** The card was swapped: open files on it are gone. */
        | 'CARD_CHANGED'
        | 'FULL'
        | 'NOT_FOUND'
        | 'EXISTS'
        /** Writing a read-only file (removing it is allowed), or a file already open for writing. */
        | 'ACCESS_DENIED'
        | 'NOT_EMPTY'
        /** Every one of the three driver file handles is in use. */
        | 'TOO_MANY_OPEN'
        | 'IS_DIRECTORY'
        | 'NOT_DIRECTORY'
        /** PS1/PocketStation cards for most operations. */
        | 'UNSUPPORTED'
        | 'NO_MEMORY'
        | 'IO'
        /** The file was closed, or lost to an IOP reset. */
        | 'CLOSED'
        | 'CANCELLED'
        /** The file is in use by another script thread. */
        | 'BUSY';

    interface Error extends globalThis.Error {
        code: ErrorCode;
        /** The card path involved, e.g. `"mc0:/MYGAME/save.json"`. */
        path?: Path;
    }

    interface Info {
        port: Port;
        type: CardType;
        /** `type !== 'none'`. */
        connected: boolean;
        formatted: boolean;
        freeClusters: number;
        /** `freeClusters * CLUSTER_SIZE`. */
        freeBytes: number;
        /**
         * A card was inserted since the previous `getInfo()` of this port
         * (also true on the first call after boot).
         */
        changed: boolean;
    }

    interface Entry {
        name: string;
        /** Full path, e.g. `"mc0:/MYGAME/save.json"` (absent for the root). */
        path?: Path;
        /** Bytes; 0 for directories. */
        size: number;
        directory: boolean;
        /** `ATTR_*` bits. */
        attributes: number;
        /** Milliseconds since 1970 (UTC), for `new Date(entry.created)`. 0 when unknown. */
        created: number;
        modified: number;
    }

    /** Binary data; strings are written as UTF-8. */
    type Data = string | ArrayBuffer | ArrayBufferView;

    interface WriteOptions {
        /** Create the missing parent directories (only checked when they are missing: no extra cost). Default `true`. */
        createDirs?: boolean;
        /**
         * Write `"<name>~"` first and swap it in only when complete, so a
         * failure or a pulled card keeps the previous file. Needs room for
         * both copies meanwhile. Default `false`: a failed write removes the
         * partial file, and the previous content is lost.
         */
        atomic?: boolean;
    }

    /* --- Card ----------------------------------------------------------- */

    /** Card status. Never throws for an empty slot: `connected` is false. */
    function getInfo(port?: Port): Info;

    /** Erases the whole card and creates an empty file system. Takes several seconds. */
    function format(port: Port): void;

    /** Erases the file system; the card reads as unformatted afterwards. */
    function unformat(port: Port): void;

    /* --- Entries -------------------------------------------------------- */

    /** Throws `NOT_FOUND` when the entry does not exist. */
    function stat(path: Path): Entry;

    /** False only for a missing entry; a missing card still throws. */
    function exists(path: Path): boolean;

    /** Directory contents, without `.` and `..`. */
    function list(path: Path): Entry[];

    /**
     * Creates a directory. Returns `false` when `recursive` found it already
     * there; without `recursive` an existing directory throws `EXISTS`.
     */
    function mkdir(path: Path, options?: { recursive?: boolean }): boolean;

    /** Removes a file or an empty directory; `recursive` removes a whole tree. */
    function remove(path: Path, options?: { recursive?: boolean }): void;

    /** Renames in place: `newName` is a name, not a path. */
    function rename(path: Path, newName: string): void;

    /**
     * Changes attributes (only `ATTR_READABLE`, `ATTR_WRITABLE`,
     * `ATTR_EXECUTABLE`, `ATTR_PROTECTED`, `ATTR_HIDDEN`) and dates.
     */
    function setInfo(path: Path, info: {
        attributes?: number;
        created?: Date | number;
        modified?: Date | number;
    }): void;

    /** Directory entries still free in `path` (each directory holds a fixed number). */
    function getFreeEntries(path: Path): number;

    /* --- Files ---------------------------------------------------------- */

    function readFile(path: Path): ArrayBuffer;

    /** Reads a file as UTF-8 text. */
    function readText(path: Path): string;

    /**
     * Reads and parses a JSON file. A parse failure throws the `SyntaxError`
     * of `JSON.parse`, with `error.path` set.
     */
    function readJSON<T = any>(path: Path): T;

    /** Creates or replaces a file. Returns the bytes written. */
    function writeFile(path: Path, data: Data, options?: WriteOptions): number;

    /** Writes `JSON.stringify(value)`; `indent` (0-10 spaces) pretty-prints it. Returns the bytes written. */
    function writeJSON(path: Path, value: unknown, options?: WriteOptions & { indent?: number }): number;

    /**
     * Opens a file for streaming. Close it when done: the driver has three
     * handles for every card together (a collected handle closes itself).
     * Modes: `"r"` read, `"r+"` read/write an existing file, `"w"` create or
     * replace and write, `"w+"` the same and read, `"a"` write starting at the
     * end (created when missing), `"a+"` the same and read.
     */
    function open(path: Path, mode?: 'r' | 'r+' | 'w' | 'w+' | 'a' | 'a+'): File;

    interface File {
        readonly __brand: 'MemoryCardFile';
        readonly closed: boolean;
        /** Bytes in the file, kept by the handle (reading it sends nothing to the card). */
        readonly size: number;
        /** Up to `size` bytes (default: the rest of the file); shorter at the end of the file. */
        read(size?: number): ArrayBuffer;
        /** Returns the bytes written. */
        write(data: Data): number;
        /** Returns the new position. */
        seek(offset: number, whence?: 'set' | 'cur' | 'end'): number;
        tell(): number;
        flush(): void;
        /**
         * Closing twice does nothing. A file lost with its card (other
         * calls throw `CARD_CHANGED`) closes without an error.
         */
        close(): void;
    }

    /* --- icon.sys ------------------------------------------------------- */

    interface IconSysOptions {
        /** Up to 33 printable ASCII characters; one `"\n"` splits the two lines. */
        title: string;
        /** Icon file (in the same save directory) shown in the list. */
        icon: string;
        /** Icon while copying. Default: `icon`. */
        copyIcon?: string;
        /** Icon while deleting. Default: `icon`. */
        deleteIcon?: string;
        /** Background opacity, 0-128. Default 96. */
        backgroundAlpha?: number;
        /** RGB 0-255 of the corners: top-left, top-right, bottom-left, bottom-right. */
        background?: [number[], number[], number[], number[]];
        /** Three light directions, components -1..1. */
        lightDirections?: [number[], number[], number[]];
        /** Three light colors, RGB 0..1. */
        lightColors?: [number[], number[], number[]];
        /** Ambient light, RGB 0..1. */
        ambient?: number[];
    }

    /**
     * Builds the `icon.sys` that makes a save directory show up in the PS2
     * browser (the title is converted to Shift-JIS). Write it next to the
     * icon files.
     */
    function createIconSys(options: IconSysOptions): ArrayBuffer;

    /* --- Background jobs ------------------------------------------------ */

    /**
     * Work running on a worker thread. `poll()` it (e.g. once per frame),
     * `wait()` for it, or `await` it: a job is a thenable that resolves with
     * the result or rejects with the `MemoryCard.Error`. Dropping the handle
     * cancels the job.
     */
    interface Job<T> extends AthenaJob<T, JobStatus<T>> {
        readonly __brand: 'MemoryCardJob';
    }

    type JobState = 'running' | 'done' | 'failed' | 'cancelled';

    interface JobStatus<T> {
        state: JobState;
        bytesDone: number;
        /** 0 until known. Remove jobs count entries in `bytesDone` instead. */
        bytesTotal: number;
        /** When `state` is `'done'`. The same value on every later poll. */
        result?: T;
        /** When `state` is `'failed'` or `'cancelled'`. */
        error?: Error;
    }

    function readFileAsync(path: Path): Job<ArrayBuffer>;
    /** Resolves with the file as UTF-8 text. */
    function readTextAsync(path: Path): Job<string>;
    /** Resolves with the parsed JSON; a parse error fails the job with the `SyntaxError`. */
    function readJSONAsync<T = any>(path: Path): Job<T>;
    /** Resolves with the bytes written. The data is copied when the job starts. */
    function writeFileAsync(path: Path, data: Data, options?: WriteOptions): Job<number>;
    /** `writeJSON` on a worker: `value` is stringified when the call is made. */
    function writeJSONAsync(path: Path, value: unknown, options?: WriteOptions & { indent?: number }): Job<number>;
    function removeAsync(path: Path, options?: { recursive?: boolean }): Job<void>;
    /** Cannot be cancelled once started. */
    function formatAsync(port: Port): Job<void>;

    /** Returns the job's progress without blocking. */
    function poll<T>(job: Job<T>): JobStatus<T>;

    /**
     * Blocks until the job settles or `timeoutMs` passes (default: no
     * limit), letting other threads run, then returns `poll(job)`.
     */
    function wait<T>(job: Job<T>, timeoutMs?: number): JobStatus<T>;

    /** Stops the job before its next block; a write removes what it wrote. */
    function cancel(job: Job<unknown>): void;

    /* --- Constants ------------------------------------------------------ */

    const ATTR_READABLE: number;
    const ATTR_WRITABLE: number;
    const ATTR_EXECUTABLE: number;
    /** Copy-protected in the browser. */
    const ATTR_PROTECTED: number;
    const ATTR_FILE: number;
    const ATTR_DIRECTORY: number;
    /** The file may not have been written completely. */
    const ATTR_CLOSED: number;
    const ATTR_PDA_EXEC: number;
    const ATTR_PS1: number;
    /** Hidden from games (the browser still shows it). */
    const ATTR_HIDDEN: number;
    const ATTR_EXISTS: number;
    /** Longest entry name, in bytes. */
    const NAME_MAX: number;
    /** Driver file handles for every card together. */
    const MAX_OPEN_FILES: number;
    /** Bytes per cluster. */
    const CLUSTER_SIZE: number;
}
