/*
 * SaveGame module: encode/decode (checksum, versions, migration, gzip
 * through a stand-in Archive), then save/load/list/remove on the card in
 * slot 1 under mc0:/ATHSAVETST, removed at the end. Runs on the host
 * (fake card of tests/js/memcard_host.c) and on PCSX2/PS2; without a
 * formatted card the card part is skipped.
 */
import * as SaveGame from "SaveGame";

let passed = 0, failed = 0, skipped = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
async function rejects(name, promise, code) {
    let error = null; try { await promise; } catch (e) { error = e; }
    check(`${name} rejects ${code}`, error !== null && error.code === code);
}
function throwsCode(name, fn, code) {
    let error = null; try { fn(); } catch (e) { error = e; }
    check(`${name} throws ${code}`, error !== null && error.code === code);
}
const DIR = "ATHSAVETST";

throwsCode("before define", () => SaveGame.encode({}), "NOT_DEFINED");
let threw = false; try { SaveGame.define({ directory: "a/b", title: "x" }); } catch (e) { threw = e instanceof TypeError; }
check("define validates the directory", threw);
SaveGame.define({ directory: DIR, title: "Athena\nSave test", version: 1, compress: false });

const data = { level: 3, name: "Zoë ✓ 𝄞", items: [1, 2, 3], nested: { ok: true } };
const bytes = SaveGame.encode(data);
check("crc32 of '123456789'", SaveGame.crc32(Uint8Array.from("123456789", c => c.charCodeAt(0))) === 0xCBF43926);
check("round trip with UTF-8", JSON.stringify(SaveGame.decode(bytes)) === JSON.stringify(data));
const corrupt = bytes.slice(0); new Uint8Array(corrupt)[25] ^= 1;
throwsCode("flipped bit", () => SaveGame.decode(corrupt), "CORRUPT");
throwsCode("truncated", () => SaveGame.decode(bytes.slice(0, bytes.byteLength - 2)), "CORRUPT");
throwsCode("not a save", () => SaveGame.decode(new ArrayBuffer(40)), "CORRUPT");
throwsCode("not JSON-able", () => SaveGame.encode(undefined), "INVALID_ARGUMENT");
throwsCode("bad slot", () => SaveGame.exists("a b"), "INVALID_ARGUMENT");

// Versions: a save from v1 read by v2 migrates; a v3 save is refused.
SaveGame.define({ directory: DIR, title: "Athena", version: 2, compress: false });
throwsCode("old save without migrate", () => SaveGame.decode(bytes), "OLD_VERSION");
SaveGame.define({ directory: DIR, title: "Athena", version: 2, compress: false,
    migrate: (d, from) => ({ ...d, migratedFrom: from }) });
check("migrate", SaveGame.decode(bytes).migratedFrom === 1);
const v2 = SaveGame.encode({ a: 1 });
SaveGame.define({ directory: DIR, title: "Athena", version: 1, compress: false });
throwsCode("newer save", () => SaveGame.decode(v2), "NEWER_VERSION");

// gzip through Archive: a stand-in on the host, the real module when present.
const realArchive = globalThis.Archive;
if (!realArchive) globalThis.Archive = { gzip: b => Uint8Array.from(b).reverse().buffer, gunzip: b => Uint8Array.from(b).reverse().buffer };
SaveGame.define({ directory: DIR, title: "Athena", version: 1 });
const packed = SaveGame.encode(data);
check("compressed flag", new Uint8Array(packed)[5] === 1);
check("compressed round trip", JSON.stringify(SaveGame.decode(packed)) === JSON.stringify(data));
if (!realArchive) delete globalThis.Archive;
if (!globalThis.Archive) throwsCode("compressed save without Archive", () => SaveGame.decode(packed), "NOT_AVAILABLE");

async function cardTests() {
    SaveGame.define({ directory: DIR, title: "Athena\nSave test", version: 1, compress: false });
    const status = SaveGame.status();
    if (!status.connected || !status.formatted) { skipped++; console.log("[SKIP] card tests: no formatted card in slot 1"); return; }
    check("empty slot loads null", (await SaveGame.load(0)) === null);
    const written = await SaveGame.save(0, data);
    check("save returns bytes", written === SaveGame.encode(data).byteLength);
    check("exists", SaveGame.exists(0) && !SaveGame.exists(1));
    check("load", JSON.stringify(await SaveGame.load(0)) === JSON.stringify(data));
    await SaveGame.save("options", { volume: 7 });
    const slots = SaveGame.list().map(s => s.slot).sort();
    check("list", slots.length === 2 && slots.includes(0) && slots.includes("options"));
    await SaveGame.save(0, { level: 4 });
    check("overwrite", (await SaveGame.load(0)).level === 4);
    // A corrupted file on the card is reported, not returned.
    MemoryCard.writeFile(`mc0:/${DIR}/slot5.sav`, corrupt);
    await rejects("corrupted slot", SaveGame.load(5), "CORRUPT");
    check("remove", (await SaveGame.remove(0)) === true && (await SaveGame.remove(0)) === false && !SaveGame.exists(0));
    MemoryCard.remove(`mc0:/${DIR}`, { recursive: true });
}

cardTests().catch(e => { failed++; console.log("[FAIL] card tests: " + e + (e.code ? ` (${e.code})` : "")); })
    .then(() => {
        console.log(`Result: ${passed} passed, ${failed} failed, ${skipped} skipped`);
        if (!failed) console.log("SaveGame module test passed");
    });
