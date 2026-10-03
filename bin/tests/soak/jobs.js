// Soak workload (tests/soak_test.js): the shared job pool. Archive
// extractions queued and running when the VM is torn down.
const FRAMES = 90;
const OUT = "tests/soak/out";

const font = new Font();
const jobs = [];

let frames = 0;
Loop.run(() => {
    font.print(40, 40, "Soak: jobs, frame " + frames + ", " + jobs.length + " started");
    if (frames % 20 === 0) {
        jobs.push(Archive.extractAsync("tests/archive/sample.zip", OUT, { overwrite: true }));
        jobs.push(Archive.extractAsync("tests/archive/sample.tar.gz", OUT, { overwrite: true }));
    }
    if (++frames === FRAMES)
        std.reload("tests/soak_test.js", { returnTo: "tests/index.js" });
});
