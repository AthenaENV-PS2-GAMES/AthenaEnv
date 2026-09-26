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
     * Work running on the shared job pool (see `AthenaJob`). The worker opens
     * its own copy of the archive and never runs script code; the script
     * awaits the job, or calls `poll()` (e.g. once per frame) to follow it.
     * Dropping the handle cancels the job.
     */
    interface Job<T> extends AthenaJob<T, JobStatus<T>> {
        readonly __brand: 'ArchiveJob';
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
