/*
 * SaveGame: versioned, checksummed save slots on the Memory Card.
 *
 * define() describes the game once (card directory, browser title and icon,
 * data version, migration); save()/load() then move plain JSON-able data in
 * and out of slots on a worker thread (MemoryCard *Async jobs, written
 * atomically: a failed or interrupted save keeps the previous one).
 *
 * File format (little endian): "ASAV", u8 format (1), u8 flags (bit 0:
 * gzip), u16 0, u32 data version, u32 payload size, u32 CRC-32 of the
 * payload, then the payload: UTF-8 JSON, gzip-compressed when the Archive
 * module is in the build and compression is on.
 */
import * as Native from "SaveGameNative";

const g = globalThis;
const MAGIC = 0x56415341; /* "ASAV" */
const FORMAT = 1;
const HEADER = 20;
const FLAG_GZIP = 1;

export class SaveError extends Error {
    constructor(code, message, cause) {
        super(message);
        this.name = "SaveGame.Error";
        this.code = code;
        if (cause !== undefined) this.cause = cause;
    }
}

let config = null;

/** CRC-32 (IEEE, as zlib) of bytes, in C. */
export function crc32(bytes) { return Native.crc32(bytes); }
const utf8Encode = text => new Uint8Array(Native.utf8Encode(text));
const utf8Decode = bytes => Native.utf8Decode(bytes);

function card() {
    const MemoryCard = g.MemoryCard;
    if (!MemoryCard) throw new SaveError("NOT_AVAILABLE", "SaveGame needs the MemoryCard module");
    return MemoryCard;
}
function requireConfig() {
    if (!config) throw new SaveError("NOT_DEFINED", "SaveGame.define() must be called first");
    return config;
}
function slotName(slot) {
    if (Number.isInteger(slot) && slot >= 0 && slot <= 99) return `slot${slot}`;
    if (typeof slot === "string" && /^[A-Za-z0-9_-]{1,20}$/.test(slot)) return slot;
    throw new SaveError("INVALID_ARGUMENT", "SaveGame slot must be 0-99 or a name of 1-20 letters, digits, _ or -");
}
function dir(c) { return `mc${c.port}:/${c.directory}`; }
function slotPath(c, slot) { return `${dir(c)}/${slotName(slot)}.sav`; }

/* Memory Card errors keep their code (NO_CARD, UNFORMATTED, FULL...). */
function wrap(error, what) {
    if (error instanceof SaveError) return error;
    return new SaveError(error && error.code ? error.code : "IO", `${what}: ${error && error.message ? error.message : error}`, error);
}

/**
 * Describes the game's saves. options: directory (card folder, 1-31
 * characters, e.g. "BASLUS-12345MYGAME"), title (browser title, a "\n"
 * splits two lines), icon (an .ico file path or its bytes; without one no
 * icon.sys is written and the save does not show in the PS2 browser),
 * version (data version, default 1), migrate(data, fromVersion) returning
 * the data for `version`, compress (default true: gzip when Archive is in
 * the build), port (0 or 1, default 0).
 */
export function define(options) {
    if (options === null || typeof options !== "object") throw new TypeError("SaveGame.define expects an object");
    const { directory, title, icon, version = 1, migrate, compress = true, port = 0 } = options;
    if (typeof directory !== "string" || !/^[^/\\*?:]{1,31}$/.test(directory))
        throw new TypeError("SaveGame directory must be 1-31 characters without / \\ * ? :");
    if (typeof title !== "string" || !title.length || title.length > 33) throw new TypeError("SaveGame title must be 1-33 characters");
    if (!Number.isInteger(version) || version < 1 || version > 0xFFFFFFFF) throw new RangeError("SaveGame version must be a positive integer");
    if (migrate !== undefined && typeof migrate !== "function") throw new TypeError("SaveGame migrate must be a function");
    if (port !== 0 && port !== 1) throw new RangeError("SaveGame port must be 0 or 1");
    let iconBytes = null;
    if (icon !== undefined) {
        if (icon instanceof ArrayBuffer) iconBytes = icon;
        else if (typeof icon === "string") iconBytes = readFile(icon);
        else throw new TypeError("SaveGame icon must be a path or an ArrayBuffer");
    }
    config = { directory, title, iconBytes, version, migrate, compress: !!compress, port, iconWritten: false };
}

function readFile(path) {
    const file = std.open(path, "rb");
    if (!file) throw new SaveError("NOT_FOUND", `SaveGame: cannot read ${path}`);
    try {
        file.seek(0, std.SEEK_END);
        const size = file.tell();
        file.seek(0, std.SEEK_SET);
        const buffer = new ArrayBuffer(size);
        if (file.read(buffer, 0, size) !== size) throw new SaveError("IO", `SaveGame: short read from ${path}`);
        return buffer;
    } finally { file.close(); }
}

/** The bytes save() writes for data (exposed for tests and tools). */
export function encode(data, options = {}) {
    const c = requireConfig();
    let json;
    try { json = JSON.stringify(data); } catch (e) { throw new SaveError("INVALID_ARGUMENT", `SaveGame: data is not JSON-able: ${e.message}`); }
    if (json === undefined) throw new SaveError("INVALID_ARGUMENT", "SaveGame: data is not JSON-able");
    let payload = utf8Encode(json), flags = 0;
    const compress = options.compress ?? c.compress;
    if (compress && g.Archive && typeof g.Archive.gzip === "function") {
        payload = new Uint8Array(g.Archive.gzip(payload, { level: 9 }));
        flags |= FLAG_GZIP;
    }
    const buffer = new ArrayBuffer(HEADER + payload.length), view = new DataView(buffer);
    view.setUint32(0, MAGIC, true); view.setUint8(4, FORMAT); view.setUint8(5, flags); view.setUint16(6, 0, true);
    view.setUint32(8, c.version, true); view.setUint32(12, payload.length, true); view.setUint32(16, crc32(payload), true);
    new Uint8Array(buffer, HEADER).set(payload);
    return buffer;
}

/** Data from save bytes: checks the header and checksum, decompresses and migrates. */
export function decode(buffer) {
    const c = requireConfig();
    if (!(buffer instanceof ArrayBuffer) || buffer.byteLength < HEADER) throw new SaveError("CORRUPT", "SaveGame: file too short");
    const view = new DataView(buffer);
    if (view.getUint32(0, true) !== MAGIC) throw new SaveError("CORRUPT", "SaveGame: not a save file");
    if (view.getUint8(4) !== FORMAT) throw new SaveError("CORRUPT", `SaveGame: unknown format ${view.getUint8(4)}`);
    const flags = view.getUint8(5), version = view.getUint32(8, true), size = view.getUint32(12, true);
    if (HEADER + size !== buffer.byteLength) throw new SaveError("CORRUPT", "SaveGame: size mismatch (truncated file)");
    let payload = new Uint8Array(buffer, HEADER, size);
    if (crc32(payload) !== view.getUint32(16, true)) throw new SaveError("CORRUPT", "SaveGame: checksum mismatch");
    if (flags & FLAG_GZIP) {
        if (!g.Archive) throw new SaveError("NOT_AVAILABLE", "SaveGame: this save is compressed and the Archive module is not in the build");
        payload = new Uint8Array(g.Archive.gunzip(payload));
    }
    let data;
    try { data = JSON.parse(utf8Decode(payload)); } catch (e) { throw new SaveError("CORRUPT", `SaveGame: bad JSON: ${e.message}`); }
    if (version > c.version) throw new SaveError("NEWER_VERSION", `SaveGame: saved by a newer version (${version} > ${c.version})`);
    if (version < c.version) {
        if (!c.migrate) throw new SaveError("OLD_VERSION", `SaveGame: version ${version} needs migrate() to reach ${c.version}`);
        data = c.migrate(data, version);
    }
    return data;
}

async function writeIcon(c, title) {
    const MemoryCard = card();
    if (!c.iconBytes || c.iconWritten) return;
    await MemoryCard.writeFileAsync(`${dir(c)}/icon.ico`, c.iconBytes);
    await MemoryCard.writeFileAsync(`${dir(c)}/icon.sys`, MemoryCard.createIconSys({ title, icon: "icon.ico" }));
    c.iconWritten = true;
}

/**
 * Saves data in slot (0-99 or a name) atomically on a worker. options.title
 * replaces the browser title (e.g. "My Game\nSlot 1 - Forest"). Resolves with
 * the bytes written; rejects with a SaveGame.Error (NO_CARD, UNFORMATTED,
 * FULL, ...).
 */
export async function save(slot, data, options = {}) {
    const c = requireConfig();
    const path = slotPath(c, slot);
    const bytes = encode(data, options);
    const MemoryCard = card();
    try {
        const info = MemoryCard.getInfo(c.port);
        if (!info.connected) throw new SaveError("NO_CARD", `SaveGame: no memory card in slot ${c.port + 1}`);
        if (!info.formatted) throw new SaveError("UNFORMATTED", `SaveGame: the memory card in slot ${c.port + 1} is not formatted`);
        const written = await MemoryCard.writeFileAsync(path, bytes, { atomic: true });
        const title = options.title ?? c.title;
        if (options.title !== undefined) c.iconWritten = false;
        await writeIcon(c, title);
        return written;
    } catch (e) { throw wrap(e, `SaveGame.save(${JSON.stringify(slot)})`); }
}

/** Loads slot: the (migrated) data, or null when the slot is empty. */
export async function load(slot) {
    const c = requireConfig();
    const path = slotPath(c, slot);
    const MemoryCard = card();
    let buffer;
    try {
        buffer = await MemoryCard.readFileAsync(path);
    } catch (e) {
        if (e && e.code === "NOT_FOUND") return null;
        throw wrap(e, `SaveGame.load(${JSON.stringify(slot)})`);
    }
    return decode(buffer);
}

/** True when slot holds a save (blocks for one card access). */
export function exists(slot) {
    const c = requireConfig();
    return card().exists(slotPath(c, slot));
}

/** Removes slot; resolves with whether it existed. */
export async function remove(slot) {
    const c = requireConfig();
    const path = slotPath(c, slot);
    const MemoryCard = card();
    try {
        if (!MemoryCard.exists(path)) return false;
        await MemoryCard.removeAsync(path);
        return true;
    } catch (e) { throw wrap(e, `SaveGame.remove(${JSON.stringify(slot)})`); }
}

/** Saved slots: name, size and modification time (blocks for one card listing). */
export function list() {
    const c = requireConfig();
    const MemoryCard = card();
    const info = MemoryCard.getInfo(c.port);
    if (!info.connected || !info.formatted || !MemoryCard.exists(dir(c))) return [];
    return MemoryCard.list(dir(c)).filter(e => !e.directory && e.name.endsWith(".sav"))
        .map(e => {
            const name = e.name.slice(0, -4), m = /^slot(\d{1,2})$/.exec(name);
            return { slot: m ? Number(m[1]) : name, size: e.size, modified: e.modified };
        });
}

/** Card state for a save menu: connected, formatted, free bytes. */
export function status() {
    const c = requireConfig();
    const info = card().getInfo(c.port);
    return { port: c.port, connected: info.connected, formatted: info.formatted, freeBytes: info.freeBytes };
}

export { SaveError as Error };
