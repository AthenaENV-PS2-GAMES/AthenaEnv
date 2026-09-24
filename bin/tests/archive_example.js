/*
 * Archive module usage example.
 *
 * Set default_script=tests/archive_example.js in athena.ini. Uses the
 * fixtures in tests/archive/ and writes to tests/archive/out_example/.
 */

const DIR = "tests/archive/";

/* Zip: inspect, then extract everything to a folder. */
const zip = Archive.open(DIR + "sample.zip");
console.log("sample.zip is a " + Archive.type(zip) + " archive");
for (const entry of Archive.list(zip)) {
    console.log("  " + entry.name + " (" + entry.size + " bytes)");
}
Archive.extractAll(zip, DIR + "out_example/zip");
Archive.close(zip);

/* Gzip: decompress a single file straight into memory. */
const gz = Archive.open(DIR + "sample.txt.gz");
const bytes = new Uint8Array(Archive.extractAll(gz));
Archive.close(gz);
let text = "";
for (const byte of bytes) text += String.fromCharCode(byte);
console.log("sample.txt.gz contains: " + text.trim());

/* Tar / tar.gz: extract to a folder. */
Archive.untar(DIR + "sample.tar.gz", DIR + "out_example/tar");

/* Errors are exceptions, not silent undefined results. */
try {
    Archive.open(DIR + "plain.txt");
} catch (error) {
    console.log("expected failure: " + error.message);
}

console.log("Archive example finished");
