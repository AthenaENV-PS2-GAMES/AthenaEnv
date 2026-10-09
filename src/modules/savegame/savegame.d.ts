/**
 * Versioned, checksummed save slots on the Memory Card.
 *
 * `define()` once, then `await save(slot, data)` and `await load(slot)`:
 * data is any JSON-able value, written atomically on a worker thread (a
 * failed or interrupted save keeps the previous one), with a CRC-32, the
 * game's data version and, when the Archive module is in the build, gzip.
 * Older saves go through `migrate()`; newer ones are refused.
 *
 * Errors are `SaveGame.Error` with a `code`: the Memory Card codes
 * (NO_CARD, UNFORMATTED, FULL, ...) plus CORRUPT, NEWER_VERSION,
 * OLD_VERSION (no migrate), NOT_DEFINED, NOT_AVAILABLE, INVALID_ARGUMENT.
 *
 * Not in the default build: `node tools/modules.js configure --modules=savegame,...`
 *
 * Example:
 * ```js
 * SaveGame.define({ directory: "MYGAME", title: "My Game", icon: "assets/icon.ico", version: 2,
 *     migrate: (data, from) => (from === 1 ? { ...data, coins: 0 } : data) });
 * await SaveGame.save(0, { level: 3, coins: 120, bindings: controls.bindings() });
 * const data = await SaveGame.load(0);   // null when empty
 * ```
 */
declare namespace SaveGame {
    type Slot = number | string;
    interface Error extends globalThis.Error {
        code: MemoryCard.ErrorCode | "CORRUPT" | "NEWER_VERSION" | "OLD_VERSION" | "NOT_DEFINED" | "NOT_AVAILABLE";
        cause?: unknown;
    }
    interface Options {
        /** Card folder, 1-31 characters. */
        directory: string;
        /** Browser title, up to 33 characters; one "\n" splits two lines. */
        title: string;
        /** .ico path or bytes; without it no icon.sys is written (the PS2 browser will not list the save). */
        icon?: string | ArrayBuffer;
        /** Data version, default 1. */
        version?: number;
        /** Upgrades data saved by an older version. */
        migrate?(data: any, fromVersion: number): any;
        /** gzip when Archive is in the build (default true). */
        compress?: boolean;
        port?: 0 | 1;
    }
    function define(options: Options): void;
    /** Slot 0-99 or a name (1-20 letters, digits, _ or -). Resolves with the bytes written. */
    function save(slot: Slot, data: unknown, options?: { title?: string; compress?: boolean }): Promise<number>;
    /** The (migrated) data, or null for an empty slot. */
    function load<T = any>(slot: Slot): Promise<T | null>;
    /** Resolves with whether the slot existed. */
    function remove(slot: Slot): Promise<boolean>;
    /** Blocks for one card access. */
    function exists(slot: Slot): boolean;
    /** Saved slots (blocks for one card listing). */
    function list(): { slot: Slot; size: number; modified: number }[];
    function status(): { port: 0 | 1; connected: boolean; formatted: boolean; freeBytes: number };
    /** The bytes save() writes / the data from them (for tools and tests). */
    function encode(data: unknown, options?: { compress?: boolean }): ArrayBuffer;
    function decode(bytes: ArrayBuffer): any;
    function crc32(bytes: Uint8Array): number;
}
