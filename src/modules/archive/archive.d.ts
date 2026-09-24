/**
 * Zip and gzip archives, plus tar / tar.gz extraction.
 *
 * Relative paths are resolved against the current directory. Extraction
 * rejects entries with absolute paths, devices (`mc0:`) or `..` segments
 * before writing anything.
 *
 * Example:
 * ```js
 * const zip = Archive.open("data.zip");
 * for (const entry of Archive.list(zip)) console.log(entry.name, entry.size);
 * Archive.extractAll(zip, "out");
 * Archive.close(zip);
 *
 * const gz = Archive.open("level.json.gz");
 * const bytes = new Uint8Array(Archive.extractAll(gz));
 * Archive.close(gz);
 *
 * Archive.untar("assets.tar.gz", "assets");
 * ```
 */
declare namespace Archive {
    /** Opaque handle returned by `Archive.open()`. */
    interface Handle {
        readonly __brand: 'Archive';
    }

    interface Entry {
        /** Path inside the archive. Directories end with `/`. */
        name: string;
        /** Uncompressed size in bytes. */
        size: number;
        /** Modification time as Unix seconds (0 when unknown). */
        mtime: number;
    }

    /** Opens a zip or gzip file (detected by content). Throws when it cannot be opened. */
    function open(path: string): Handle;

    /** Returns the detected archive format. */
    function type(archive: Handle): 'zip' | 'gz';

    /** Lists the entries of a zip archive. Throws a TypeError for gzip. */
    function list(archive: Handle): Entry[];

    /**
     * Zip: writes every entry below `destination` (default: current directory).
     * Gzip: returns the decompressed data; `destination` is not accepted.
     */
    function extractAll(archive: Handle, destination?: string): ArrayBuffer | undefined;

    /** Releases the archive. Do not use `archive` afterwards. */
    function close(archive: Handle): void;

    /**
     * Extracts a `.tar` or `.tar.gz` file below `destination`
     * (default: current directory).
     */
    function untar(path: string, destination?: string): void;
}
