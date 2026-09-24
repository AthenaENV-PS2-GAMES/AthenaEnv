/*
 * Archive module smoke and contract tests.
 *
 * Run with bin/ as the working directory (default_script=tests/archive_test.js
 * in athena.ini). Fixtures live in tests/archive/ and are regenerated with
 * tests/archive/make_fixtures.py. Extracted files are written to
 * tests/archive/out_*. The suite is idempotent: it never depends on deleting
 * files, since remove() on host: is not available under PCSX2.
 */

const DIR = "tests/archive/";
const HELLO = "Hello from AthenaEnv Archive!\n";
const NESTED = "nested file content\n".repeat(3);
const LONG_NAME = "long/" + "x".repeat(120) + ".txt";
const USTAR_NAME = "a".repeat(60) + "/" + "b".repeat(60) + ".txt";

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

function captureError(callback) {
    try {
        callback();
    } catch (error) {
        return error;
    }
    throw new Error("expected an exception");
}

function expectThrow(name, callback, type) {
    test(name, function() {
        const error = captureError(callback);
        if (type && !(error instanceof type))
            throw new Error("expected " + type.name + ", got " + error);
    });
}

/* Expects an Archive error with the given error.code (and constructor). */
function expectCode(name, callback, code, type) {
    test(name, function() {
        const error = captureError(callback);
        if (error.code !== code)
            throw new Error("expected code " + code + ", got " + error.code + " (" + error + ")");
        if (type && !(error instanceof type))
            throw new Error("expected " + type.name + ", got " + error);
    });
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
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

function readBytes(path) {
    const file = std.open(path, "rb");
    if (!file) throw new Error("cannot open " + path);
    file.seek(0, std.SEEK_END);
    const size = file.tell();
    file.seek(0, std.SEEK_SET);
    const buffer = new ArrayBuffer(size);
    file.read(buffer, 0, size);
    file.close();
    return buffer;
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

function bytesToString(buffer) {
    const bytes = new Uint8Array(buffer);
    let text = "";
    for (let i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]);
    return text;
}

function stringToBytes(text) {
    const bytes = new Uint8Array(text.length);
    for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i);
    return bytes;
}

function withArchive(path, callback) {
    const archive = Archive.open(path);
    try {
        return callback(archive);
    } finally {
        Archive.close(archive);
    }
}

function entryNames(archive) {
    return Archive.list(archive).map(function(entry) { return entry.name; });
}

console.log("[test] Archive loaded");

test("Archive exports are available", function() {
    for (const name of ["open", "close", "type", "list", "read", "extractAll",
        "extract", "untar", "gunzip", "gzip"]) {
        assert(typeof Archive[name] === "function", name + " is missing");
    }
});

/* --- zip --------------------------------------------------------------- */

test("zip: open returns an opaque handle", function() {
    withArchive(DIR + "sample.zip", function(zip) {
        assert(typeof zip === "object" && zip !== null, "invalid handle");
        assertEqual(Archive.type(zip), "zip", "type");
    });
});

test("zip: list describes every entry", function() {
    const entries = withArchive(DIR + "sample.zip", Archive.list);
    assertEqual(entries.length, 3, "entry count");
    const byName = {};
    for (const entry of entries) {
        assertEqual(typeof entry.name, "string", "name type");
        assertEqual(typeof entry.size, "number", "size type");
        assertEqual(typeof entry.compressedSize, "number", "compressedSize type");
        assertEqual(typeof entry.mtime, "number", "mtime type");
        assertEqual(typeof entry.dir, "boolean", "dir type");
        assertEqual(entry.encrypted, false, entry.name + " encrypted");
        byName[entry.name] = entry;
    }
    assertEqual(byName["readme.txt"].size, HELLO.length, "readme.txt size");
    assertEqual(byName["readme.txt"].dir, false, "readme.txt dir");
    assertEqual(byName["data/nested.txt"].size, NESTED.length, "data/nested.txt size");
    assertEqual(byName["data/"].dir, true, "data/ dir");
    assert(byName["readme.txt"].mtime > 0, "mtime missing");
});

test("zip: read returns one entry in memory", function() {
    withArchive(DIR + "sample.zip", function(zip) {
        const data = Archive.read(zip, "data/nested.txt");
        assert(data instanceof ArrayBuffer, "expected an ArrayBuffer");
        assertEqual(bytesToString(data), NESTED, "content");
        assertEqual(bytesToString(Archive.read(zip, "readme.txt")), HELLO, "second entry");
    });
});

expectCode("zip: read reports a missing entry", function() {
    withArchive(DIR + "sample.zip", function(zip) { Archive.read(zip, "nope.txt"); });
}, "NOT_FOUND");

expectCode("zip: read rejects a directory", function() {
    withArchive(DIR + "sample.zip", function(zip) { Archive.read(zip, "data/"); });
}, "UNSUPPORTED", TypeError);

expectCode("zip: read requires an entry name", function() {
    withArchive(DIR + "sample.zip", function(zip) { Archive.read(zip); });
}, "INVALID_ARGUMENT", TypeError);

test("zip: extractAll writes files and returns the count", function() {
    const dest = DIR + "out_zip";
    const count = withArchive(DIR + "sample.zip", function(zip) {
        return Archive.extractAll(zip, dest);
    });
    assertEqual(count, 3, "written entries");
    assertEqual(readText(dest + "/readme.txt"), HELLO, "readme.txt content");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "data/nested.txt content");
});

expectCode("zip: path traversal is rejected", function() {
    withArchive(DIR + "evil.zip", function(zip) { Archive.extractAll(zip, DIR + "out_evil"); });
}, "UNSAFE_PATH");

test("zip: a rejected archive writes nothing", function() {
    assert(!exists(DIR + "escape.txt"), "file escaped the destination");
    assert(!exists(DIR + "out_evil/ok.txt"), "archive was partially extracted");
});

test("zip: the error names the offending entry", function() {
    const error = captureError(function() {
        withArchive(DIR + "evil.zip", function(zip) { Archive.extractAll(zip, DIR + "out_evil"); });
    });
    assert(error.message.indexOf("../escape.txt") >= 0, "entry missing from: " + error.message);
});

expectCode("zip: read refuses entries past the memory limit", function() {
    withArchive(DIR + "bomb.zip", function(zip) { Archive.read(zip, "zeros.bin"); });
}, "TOO_LARGE", RangeError);

expectCode("zip: extractAll enforces maxSize before writing", function() {
    withArchive(DIR + "bomb.zip", function(zip) {
        Archive.extractAll(zip, DIR + "out_bomb", { maxSize: 1024 });
    });
}, "TOO_LARGE", RangeError);

test("zip: nothing is written past maxSize", function() {
    assert(!exists(DIR + "out_bomb/zeros.bin"), "bomb was extracted");
});

test("zip: encrypted entries are flagged", function() {
    const entries = withArchive(DIR + "encrypted.zip", Archive.list);
    assertEqual(entries[0].encrypted, true, "encrypted flag");
});

expectCode("zip: encrypted entries cannot be read", function() {
    withArchive(DIR + "encrypted.zip", function(zip) { Archive.read(zip, "secret.txt"); });
}, "ENCRYPTED");

expectCode("zip: encrypted entries cannot be extracted", function() {
    withArchive(DIR + "encrypted.zip", function(zip) { Archive.extractAll(zip, DIR + "out_encrypted"); });
}, "ENCRYPTED");

/* --- gzip -------------------------------------------------------------- */

test("gz: a gzip file is a single-entry archive", function() {
    withArchive(DIR + "sample.txt.gz", function(gz) {
        assertEqual(Archive.type(gz), "gz", "type");
        const entries = Archive.list(gz);
        assertEqual(entries.length, 1, "entry count");
        assertEqual(entries[0].name, "sample.txt", "name from the gzip header");
        assertEqual(entries[0].size, HELLO.length, "size from the trailer");
        assertEqual(entries[0].mtime, 1700000000, "mtime from the header");
    });
});

test("gz: read works with and without the entry name", function() {
    withArchive(DIR + "sample.txt.gz", function(gz) {
        assertEqual(bytesToString(Archive.read(gz)), HELLO, "without name");
        assertEqual(bytesToString(Archive.read(gz, "sample.txt")), HELLO, "with name");
    });
});

test("gz: extractAll writes the decompressed file", function() {
    const dest = DIR + "out_gz";
    const count = withArchive(DIR + "sample.txt.gz", function(gz) {
        return Archive.extractAll(gz, dest);
    });
    assertEqual(count, 1, "written entries");
    assertEqual(readText(dest + "/sample.txt"), HELLO, "content");
});

expectCode("gz: read stops a decompression bomb", function() {
    withArchive(DIR + "bomb.bin.gz", function(gz) { Archive.read(gz); });
}, "TOO_LARGE", RangeError);

/* --- tar --------------------------------------------------------------- */

test("tar: list, read and extractAll", function() {
    const dest = DIR + "out_tar";
    withArchive(DIR + "sample.tar", function(tar) {
        assertEqual(Archive.type(tar), "tar", "type");
        assertEqual(entryNames(tar).join(","), "readme.txt,data/nested.txt,empty_dir/", "entries");
        assertEqual(bytesToString(Archive.read(tar, "data/nested.txt")), NESTED, "read");
        assertEqual(Archive.extractAll(tar, dest), 3, "written entries");
    });
    assertEqual(readText(dest + "/readme.txt"), HELLO, "readme.txt content");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "data/nested.txt content");
    assert(exists(dest + "/empty_dir"), "empty_dir was not created");
});

test("tar.gz: detected as tar and read in any order", function() {
    withArchive(DIR + "sample.tar.gz", function(tar) {
        assertEqual(Archive.type(tar), "tar", "type");
        const entries = Archive.list(tar);
        assertEqual(entries.length, 3, "entry count");
        /* Backwards seek on a compressed stream. */
        assertEqual(bytesToString(Archive.read(tar, "data/nested.txt")), NESTED, "second entry");
        assertEqual(bytesToString(Archive.read(tar, "readme.txt")), HELLO, "first entry");
    });
});

expectCode("tar: path traversal is rejected", function() {
    Archive.extract(DIR + "evil.tar", DIR + "out_evil_tar");
}, "UNSAFE_PATH");

test("tar: a rejected archive writes nothing", function() {
    assert(!exists(DIR + "out_evil_tar/ok.txt"), "safe entry was written before the unsafe one");
    assert(!exists(DIR + "escape.txt"), "file escaped the destination");
});

test("tar: long USTAR, GNU and pax names", function() {
    const cases = [
        ["longnames.tar", USTAR_NAME, "ustar"],
        ["longnames_gnu.tar", LONG_NAME, "gnu"],
        ["longnames_pax.tar", LONG_NAME, "pax"],
    ];
    for (const [file, name, content] of cases) {
        withArchive(DIR + file, function(tar) {
            assertEqual(entryNames(tar).join(","), name, file + " name");
            assertEqual(bytesToString(Archive.read(tar, name)), content, file + " content");
        });
    }
});

/* --- extraction options -------------------------------------------------- */

test("extract: shortcut and untar alias return the count", function() {
    assertEqual(Archive.extract(DIR + "sample.tar.gz", DIR + "out_targz"), 3, "extract");
    assertEqual(Archive.untar(DIR + "sample.tar.gz", DIR + "out_targz"), 3, "untar");
    assertEqual(readText(DIR + "out_targz/data/nested.txt"), NESTED, "content");
});

test("options: filter selects entries", function() {
    const seen = [];
    const count = Archive.extract(DIR + "sample.zip", DIR + "out_filter", {
        filter: function(entry) {
            seen.push(entry.name);
            return entry.name === "readme.txt";
        },
    });
    assertEqual(count, 1, "written entries");
    assertEqual(seen.length, 3, "filter calls");
    assertEqual(readText(DIR + "out_filter/readme.txt"), HELLO, "selected entry");
    assert(!exists(DIR + "out_filter/data/nested.txt"), "filtered entry was written");
});

test("options: onProgress reports every selected entry", function() {
    const calls = [];
    Archive.extract(DIR + "sample.zip", DIR + "out_progress", {
        onProgress: function(entry, index, count) {
            calls.push(index + "/" + count + ":" + entry.name);
        },
    });
    assertEqual(calls.join(","), "0/3:readme.txt,1/3:data/,2/3:data/nested.txt", "calls");
});

expectCode("options: onProgress returning false cancels", function() {
    Archive.extract(DIR + "sample.zip", DIR + "out_progress", {
        onProgress: function() { return false; },
    });
}, "ABORTED");

test("options: an exception in a callback propagates", function() {
    const error = captureError(function() {
        Archive.extract(DIR + "sample.zip", DIR + "out_progress", {
            filter: function() { throw new Error("filter exploded"); },
        });
    });
    assertEqual(error.message, "filter exploded", "message");
});

expectCode("options: the handle is busy inside a callback", function() {
    withArchive(DIR + "sample.zip", function(zip) {
        Archive.extractAll(zip, DIR + "out_progress", {
            filter: function() { Archive.close(zip); return true; },
        });
    });
}, "BUSY");

expectCode("options: overwrite false refuses existing files", function() {
    const dest = DIR + "out_exists";
    Archive.extract(DIR + "sample.zip", dest);
    Archive.extract(DIR + "sample.zip", dest, { overwrite: false });
}, "EXISTS");

expectThrow("options: rejects a non-object", function() {
    Archive.extract(DIR + "sample.zip", DIR + "out_progress", 1);
}, TypeError);

expectThrow("options: rejects a non-boolean overwrite", function() {
    Archive.extract(DIR + "sample.zip", DIR + "out_progress", { overwrite: "yes" });
}, TypeError);

expectThrow("options: rejects a negative maxSize", function() {
    Archive.extract(DIR + "sample.zip", DIR + "out_progress", { maxSize: -1 });
}, RangeError);

expectThrow("options: rejects a non-function filter", function() {
    Archive.extract(DIR + "sample.zip", DIR + "out_progress", { filter: true });
}, TypeError);

/* --- in-memory gzip ------------------------------------------------------ */

test("gzip/gunzip: round trip with typed arrays and ArrayBuffers", function() {
    const text = HELLO.repeat(50);
    const packed = Archive.gzip(stringToBytes(text), { level: 9 });
    assert(packed instanceof ArrayBuffer, "gzip returns an ArrayBuffer");
    assert(packed.byteLength < text.length, "data was not compressed");
    assertEqual(bytesToString(Archive.gunzip(packed)), text, "ArrayBuffer input");
    assertEqual(bytesToString(Archive.gunzip(new Uint8Array(packed))), text, "Uint8Array input");
});

test("gzip/gunzip: empty input", function() {
    assertEqual(Archive.gunzip(Archive.gzip(new ArrayBuffer(0))).byteLength, 0, "empty round trip");
});

test("gunzip: reads gzip files loaded by the script", function() {
    assertEqual(bytesToString(Archive.gunzip(readBytes(DIR + "sample.txt.gz"))), HELLO, "content");
});

expectCode("gunzip: stops at maxSize", function() {
    Archive.gunzip(readBytes(DIR + "bomb.bin.gz"), { maxSize: 1024 });
}, "TOO_LARGE", RangeError);

expectCode("gunzip: rejects data that is not gzip", function() {
    Archive.gunzip(stringToBytes("not gzip data at all"));
}, "BAD_FORMAT");

expectThrow("gzip: rejects an invalid level", function() {
    Archive.gzip(new ArrayBuffer(4), { level: 10 });
}, RangeError);

expectThrow("gzip: rejects non-binary input", function() {
    Archive.gzip("text");
}, TypeError);

/* --- unsupported compression -------------------------------------------- */

expectCode("zip: other compression methods cannot be read", function() {
    withArchive(DIR + "bzip2.zip", function(zip) { Archive.read(zip, "packed.txt"); });
}, "UNSUPPORTED", TypeError);

test("zip: other compression methods are rejected before writing", function() {
    const error = captureError(function() { Archive.extract(DIR + "bzip2.zip", DIR + "out_bzip2"); });
    assertEqual(error.code, "UNSUPPORTED", "code");
    assert(!exists(DIR + "out_bzip2/packed.txt"), "entry was written");
});

/* --- background jobs ------------------------------------------------------ */

/* Polls like a frame loop would, with a bound so a stuck job fails the test. */
function pollUntilSettled(job) {
    for (let i = 0; i < 20000; i++) {
        const status = Archive.poll(job);
        if (status.state !== "running") return status;
        System.sleep(1);
    }
    throw new Error("job did not settle");
}

test("async: extractAsync reports progress and the count", function() {
    const dest = DIR + "out_async";
    const job = Archive.extractAsync(DIR + "sample.tar.gz", dest);
    assert(typeof job === "object" && job !== null, "invalid job handle");
    const status = pollUntilSettled(job);
    assertEqual(status.state, "done", "state");
    assertEqual(status.result, 3, "result");
    assertEqual(status.entriesDone, 3, "entriesDone");
    assertEqual(status.entriesTotal, 3, "entriesTotal");
    assertEqual(status.bytesTotal, HELLO.length + NESTED.length, "bytesTotal");
    assertEqual(status.bytesDone, status.bytesTotal, "bytesDone");
    assertEqual(status.entry, "", "entry after completion");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "content");
});

test("async: include selects entries and directory prefixes", function() {
    const dest = DIR + "out_async_include";
    const status = Archive.wait(Archive.extractAsync(DIR + "sample.zip", dest, { include: ["data/"] }));
    assertEqual(status.state, "done", "state");
    assertEqual(status.result, 2, "result");
    assertEqual(readText(dest + "/data/nested.txt"), NESTED, "selected entry");
    assert(!exists(dest + "/readme.txt"), "excluded entry was written");
});

test("async: readAsync returns the same ArrayBuffer on every poll", function() {
    const job = Archive.readAsync(DIR + "sample.zip", "readme.txt");
    const status = Archive.wait(job);
    assertEqual(status.state, "done", "state");
    assert(status.result instanceof ArrayBuffer, "expected an ArrayBuffer");
    assertEqual(bytesToString(status.result), HELLO, "content");
    assert(Archive.poll(job).result === status.result, "result changed between polls");
});

test("async: readAsync on a .gz without a name", function() {
    const status = Archive.wait(Archive.readAsync(DIR + "sample.txt.gz"));
    assertEqual(status.state, "done", "state");
    assertEqual(bytesToString(status.result), HELLO, "content");
});

test("async: failures are reported as error objects", function() {
    const cases = [
        [Archive.readAsync(DIR + "bomb.bin.gz"), "TOO_LARGE", RangeError],
        [Archive.extractAsync(DIR + "evil.zip", DIR + "out_async_evil"), "UNSAFE_PATH", InternalError],
        [Archive.extractAsync(DIR + "does_not_exist.zip"), "IO", InternalError],
        [Archive.readAsync(DIR + "sample.zip", "nope.txt"), "NOT_FOUND", InternalError],
    ];
    for (const [job, code, type] of cases) {
        const status = Archive.wait(job);
        assertEqual(status.state, "failed", code + " state");
        assert(status.error instanceof type, code + ": expected " + type.name + ", got " + status.error);
        assertEqual(status.error.code, code, "error.code");
        assert(Archive.poll(job).error === status.error, code + ": error changed between polls");
    }
    assert(!exists(DIR + "out_async_evil/ok.txt"), "unsafe archive was partially extracted");
});

test("async: cancel stops the job and removes the partial file", function() {
    const dest = DIR + "out_async_cancel";
    const job = Archive.extractAsync(DIR + "bomb.zip", dest);
    Archive.cancel(job);
    const status = Archive.wait(job);
    assertEqual(status.state, "cancelled", "state");
    assertEqual(status.error.code, "ABORTED", "error.code");
    assert(!exists(dest + "/zeros.bin"), "partial file was kept");
});

test("async: wait with a timeout returns while the job runs", function() {
    const job = Archive.extractAsync(DIR + "bomb.zip", DIR + "out_async_timeout");
    const status = Archive.wait(job, 0);
    assert(status.state === "running" || status.state === "done", "unexpected state " + status.state);
    Archive.cancel(job);
    Archive.wait(job);
});

expectThrow("async: callbacks are rejected with a hint", function() {
    Archive.extractAsync(DIR + "sample.zip", DIR + "out_async", { onProgress: function() {} });
}, TypeError);

expectThrow("async: include must be an array of strings", function() {
    Archive.extractAsync(DIR + "sample.zip", DIR + "out_async", { include: "data/" });
}, TypeError);

expectThrow("async: include rejects empty names", function() {
    Archive.extractAsync(DIR + "sample.zip", DIR + "out_async", { include: [""] });
}, TypeError);

expectThrow("async: poll rejects other values", function() {
    Archive.poll({});
}, TypeError);

expectThrow("async: wait rejects a negative timeout", function() {
    Archive.wait(Archive.readAsync(DIR + "sample.txt.gz"), -1);
}, RangeError);

test("async: dropped jobs are cancelled by the finalizer", function() {
    /* The handles are dropped at once: each finalizer cancels and joins its worker. */
    for (let i = 0; i < 4; i++) Archive.extractAsync(DIR + "sample.tar.gz", DIR + "out_async_dropped" + i);
    std.gc();
});

/* --- errors -------------------------------------------------------------- */

expectCode("open: missing file", function() {
    Archive.open(DIR + "does_not_exist.zip");
}, "IO", InternalError);

expectCode("open: file that is not an archive", function() {
    Archive.open(DIR + "plain.txt");
}, "BAD_FORMAT", InternalError);

expectCode("extract: missing file", function() {
    Archive.extract(DIR + "does_not_exist.tar");
}, "IO", InternalError);

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

expectCode("list rejects values from another type", function() {
    Archive.list({});
}, "CLOSED", TypeError);

expectCode("close rejects an already closed archive", function() {
    const zip = Archive.open(DIR + "sample.zip");
    Archive.close(zip);
    Archive.close(zip);
}, "CLOSED", TypeError);

expectCode("read rejects a closed archive", function() {
    const zip = Archive.open(DIR + "sample.zip");
    Archive.close(zip);
    Archive.read(zip, "readme.txt");
}, "CLOSED", TypeError);

test("unclosed handles are released by the finalizer", function() {
    for (let i = 0; i < 16; i++) Archive.open(DIR + "sample.tar.gz");
    std.gc();
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Archive tests failed");
