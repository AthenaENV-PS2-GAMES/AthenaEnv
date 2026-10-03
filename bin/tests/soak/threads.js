// Soak workload (tests/soak_test.js): JavaScript threads. Workers running
// (and sleeping in native code) when the VM is torn down.
const FRAMES = 90;
const WORKERS = 3;

const font = new Font();
const counts = new Array(WORKERS).fill(0);
for (let i = 0; i < WORKERS; i++) {
    const thread = Thread.new(function () {
        for (;;) {
            counts[i]++;
            if (counts[i] % 50 === 0) System.sleep(1);
        }
    }, "Soak worker " + i);
    Thread.start(thread);
}

let frames = 0;
Loop.run(() => {
    font.print(40, 40, "Soak: threads, frame " + frames + ", " + counts.join(" "));
    if (++frames === FRAMES)
        std.reload("tests/soak_test.js", { returnTo: "tests/index.js" });
});
