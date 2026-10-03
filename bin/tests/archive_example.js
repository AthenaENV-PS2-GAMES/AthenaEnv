/*
 * Archive module usage example.
 *
 * Set default_script=tests/archive_example.js in athena.ini. Uses the
 * fixtures in tests/archive/ and writes to tests/archive/out_example/.
 */

const DIR = "tests/archive/";

function text(buffer) {
    let out = "";
    for (const byte of new Uint8Array(buffer)) out += String.fromCharCode(byte);
    return out;
}

/* Every format is a list of entries: zip, tar, tar.gz and plain .gz alike. */
for (const file of ["sample.zip", "sample.tar.gz", "sample.txt.gz"]) {
    const archive = Archive.open(DIR + file);
    console.log(file + " (" + Archive.type(archive) + ")");
    for (const entry of Archive.list(archive)) {
        console.log("  " + (entry.dir ? "[dir] " : "") + entry.name + " " + entry.size + " bytes");
    }
    Archive.close(archive);
}

/* Read an asset straight from the archive, without touching the disk. */
const pack = Archive.open(DIR + "sample.zip");
console.log("readme.txt says: " + text(Archive.read(pack, "readme.txt")).trim());
Archive.close(pack);

/* Install with progress; nothing is written if any entry is unsafe. */
const written = Archive.extract(DIR + "sample.tar.gz", DIR + "out_example", {
    onProgress(entry, index, count) {
        console.log("  [" + (index + 1) + "/" + count + "] " + entry.name);
    },
});
console.log("extracted " + written + " entries");

/* Compress data in memory, e.g. before writing a save. */
const save = new Uint8Array(4096).fill(42);
const packed = Archive.gzip(save, { level: 9 });
console.log("save: " + save.length + " -> " + packed.byteLength + " bytes");
console.log("restored: " + Archive.gunzip(packed).byteLength + " bytes");

/*
 * Background extraction: the worker thread does the I/O while this loop
 * keeps running (a game would draw a progress bar and call Screen.flip()).
 */
const job = Archive.extractAsync(DIR + "sample.zip", DIR + "out_example/async", {
    include: ["data/"],
});
let status = Archive.poll(job);
while (status.state === "running") {
    console.log("  async: " + status.bytesDone + "/" + status.bytesTotal + " bytes " + status.entry);
    System.sleep(1);
    status = Archive.poll(job);
}
console.log("async extraction " + status.state + ": " +
    (status.state === "done" ? status.result + " entries" : status.error.message));

/* Failures carry a stable error.code. */
try {
    Archive.extract(DIR + "evil.zip", DIR + "out_example/evil");
} catch (error) {
    console.log("rejected (" + error.code + "): " + error.message);
}

console.log("Archive example finished");
