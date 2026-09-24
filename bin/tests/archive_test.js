/*
 * Archive module smoke and contract tests.
 *
 * Run with bin/ as the working directory (default_script=tests/archive_test.js
 * in athena.ini). Fixtures live in tests/archive/ and are regenerated with
 * tests/archive/make_fixtures.py. Extracted files are written to
 * tests/archive/out*.
 */

const DIR = "tests/archive/";
const HELLO = "Hello from AthenaEnv Archive!\n";
const NESTED = "nested file content\n".repeat(3);

let passed = 0;
let failed = 0;

function pass(name) {
    console.log("[PASS] " + name);
    passed++;
}

function fail(name, error) {
    console.log("[FAIL] " + name + ": " + error);
    failed++;
}

function test(name, callback) {
    try {
        callback();
        pass(name);
    } catch (error) {
        fail(name, error);
    }
}

function expectThrow(name, callback, type) {
    test(name, function() {
        let threw = null;
        try {
            callback();
        } catch (error) {
            threw = error;
        }
        if (!threw) throw new Error("expected an exception");
        if (type && !(threw instanceof type))
            throw new Error("expected " + type.name + ", got " + threw);
    });
}

function assertEqual(actual, expected, what) {
    if (actual !== expected)
        throw new Error(what + ": expected " + JSON.stringify(expected) + ", got " + JSON.stringify(actual));
}

function readText(path) {
    const text = std.loadFile(path);
    if (text === null) throw new Error("cannot read " + path);
    return text;
}

/* os.stat is stubbed in this QuickJS port: probe files with std.open and
 * directories through their parent's listing. */
function exists(path) {
    const file = std.open(path, "rb");
    if (file) {
        file.close();
        return true;
    }
    const slash = path.lastIndexOf("/");
    const parent = slash < 0 ? "" : path.substring(0, slash);
    const name = path.substring(slash + 1);
    let entries;
    try {
        entries = System.listDir(parent);
    } catch (error) {
        return false;
    }
    return !!entries && entries.some(function(entry) {
        return entry.name === name;
    });
}

function removeQuiet(path) {
    os.remove(path);
}

function bytesToString(buffer) {
    const bytes = new Uint8Array(buffer);
    let text = "";
    for (let i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]);
    return text;
}

console.log("[test] Archive imported");

test("Archive exports are available", function() {
    for (const name of ["open", "type", "list", "extractAll", "close", "untar"]) {
        if (typeof Archive[name] !== "function") throw new Error(name + " is missing");
    }
});

/* --- zip --------------------------------------------------------------- */

test("open zip returns an opaque handle", function() {
    const zip = Archive.open(DIR + "sample.zip");
    if (typeof zip !== "object" || zip === null) throw new Error("invalid handle");
    assertEqual(Archive.type(zip), "zip", "type");
    Archive.close(zip);
});

test("list zip entries", function() {
    const zip = Archive.open(DIR + "sample.zip");
    const entries = Archive.list(zip);
    Archive.close(zip);

    assertEqual(entries.length, 3, "entry count");
    const byName = {};
    for (const entry of entries) {
        if (typeof entry.name !== "string") throw new Error("name is not a string");
        if (typeof entry.size !== "number") throw new Error("size is not a number");
        if (typeof entry.mtime !== "number") throw new Error("mtime is not a number");
        byName[entry.name] = entry;
    }
    assertEqual(byName["readme.txt"].size, HELLO.length, "readme.txt size");
    assertEqual(byName["data/nested.txt"].size, NESTED.length, "data/nested.txt size");
    if (!byName["data/"]) throw new Error("directory entry missing");
    if (byName["readme.txt"].mtime <= 0) throw new Error("mtime missing");
});

test("extractAll zip writes files and directories", function() {
    const dest = DIR + "out_zip";
    const zip = Archive.open(DIR + "sample.zip");
    const result = Archive.extractAll(zip, dest);
    Archive.close(zip);

    assertEqual(result, undefined, "zip extractAll result");
    assertEqual(readText(dest + "/readme.txt"), HELLO, "readme.txt content");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "data/nested.txt content");
});

test("extractAll zip rejects path traversal and writes nothing", function() {
    const dest = DIR + "out_evil";
    removeQuiet(dest + "/ok.txt");
    removeQuiet(DIR + "escape.txt");

    const zip = Archive.open(DIR + "evil.zip");
    let threw = false;
    try {
        Archive.extractAll(zip, dest);
    } catch (error) {
        threw = true;
    }
    Archive.close(zip);

    if (!threw) throw new Error("unsafe archive was accepted");
    if (exists(DIR + "escape.txt")) throw new Error("file escaped the destination");
    if (exists(dest + "/ok.txt")) throw new Error("archive was partially extracted");
});

/* --- gzip -------------------------------------------------------------- */

test("extractAll gzip returns the decompressed data", function() {
    const gz = Archive.open(DIR + "sample.txt.gz");
    assertEqual(Archive.type(gz), "gz", "type");
    const data = Archive.extractAll(gz);
    if (!(data instanceof ArrayBuffer)) throw new Error("expected an ArrayBuffer");
    assertEqual(bytesToString(data), HELLO, "gzip content");

    /* The stream is rewound, so a second read returns the same data. */
    assertEqual(bytesToString(Archive.extractAll(gz)), HELLO, "second read");
    Archive.close(gz);
});

expectThrow("list rejects gzip archives", function() {
    const gz = Archive.open(DIR + "sample.txt.gz");
    try {
        Archive.list(gz);
    } finally {
        Archive.close(gz);
    }
}, TypeError);

expectThrow("extractAll gzip rejects a destination", function() {
    const gz = Archive.open(DIR + "sample.txt.gz");
    try {
        Archive.extractAll(gz, DIR + "out_gz");
    } finally {
        Archive.close(gz);
    }
}, TypeError);

/* --- tar --------------------------------------------------------------- */

test("untar extracts a .tar", function() {
    const dest = DIR + "out_tar";
    Archive.untar(DIR + "sample.tar", dest);
    assertEqual(readText(dest + "/readme.txt"), HELLO, "readme.txt content");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "data/nested.txt content");
    if (!exists(dest + "/empty_dir")) throw new Error("empty_dir was not created");
});

test("untar extracts a .tar.gz without an intermediate .tar", function() {
    const dest = DIR + "out_targz";
    Archive.untar(DIR + "sample.tar.gz", dest);
    assertEqual(readText(dest + "/readme.txt"), HELLO, "readme.txt content");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "data/nested.txt content");
});

/* --- errors ------------------------------------------------------------ */

expectThrow("open rejects a missing file", function() {
    Archive.open(DIR + "does_not_exist.zip");
}, InternalError);

expectThrow("open rejects a non-archive file", function() {
    Archive.open(DIR + "plain.txt");
}, InternalError);

expectThrow("untar rejects a missing file", function() {
    Archive.untar(DIR + "does_not_exist.tar");
}, InternalError);

expectThrow("open rejects missing arguments", function() {
    Archive.open();
}, TypeError);

expectThrow("open rejects extra arguments", function() {
    Archive.open(DIR + "sample.zip", 1);
}, TypeError);

expectThrow("open rejects non-string paths", function() {
    Archive.open(42);
}, TypeError);

expectThrow("open rejects empty paths", function() {
    Archive.open("");
}, TypeError);

expectThrow("list rejects values from another type", function() {
    Archive.list({});
}, TypeError);

expectThrow("close rejects an already closed archive", function() {
    const zip = Archive.open(DIR + "sample.zip");
    Archive.close(zip);
    Archive.close(zip);
}, TypeError);

expectThrow("list rejects a closed archive", function() {
    const zip = Archive.open(DIR + "sample.zip");
    Archive.close(zip);
    Archive.list(zip);
}, TypeError);

test("unclosed handles are released by the finalizer", function() {
    for (let i = 0; i < 16; i++) Archive.open(DIR + "sample.zip");
    std.gc();
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Archive tests failed");
