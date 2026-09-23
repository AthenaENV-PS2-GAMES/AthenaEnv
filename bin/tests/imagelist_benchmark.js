// ImageList benchmark (roadmap step 7).
//
// Loads FILES ROUNDS times with each configuration inside a vsync frame loop
// and prints one line per configuration. Add larger PNG/JPEG assets to FILES
// for meaningful numbers; each file is decoded again every round because the
// cache is disabled. Times come from clock(), so their resolution depends on
// the toolchain's CLOCKS_PER_SEC.

const FILES = ["tests/my_image.png"];
const ROUNDS = 10;
const MAX_FRAMES = 5000;

const CONFIGS = [
    { name: "cooperative", options: {}, upload: "draw" },
    { name: "cooperative+bind", options: {}, upload: "bind" },
    { name: "worker", options: { workers: 1 }, upload: "draw" },
    { name: "worker+bind", options: { workers: 1 }, upload: "bind" },
    {
        name: "worker+ahead",
        options: { workers: 1, maxMemory: 1024 * 1024 },
        upload: "draw",
    },
];

function fixed(value) {
    return value.toFixed(2);
}

function run(config) {
    const list = new ImageList(config.options);
    const ready = [];
    const baseMemory = System.getUsedMemory();
    let peakMemory = baseMemory;
    let frames = 0;
    let loads = 0;
    let errors = 0;
    let processTotal = 0;
    let processWorst = 0;
    let frameWorst = 0;
    const total = ROUNDS * FILES.length;
    let queued = 0;
    const start = System.getMilliseconds();

    // Keeps up to one request per file pending. Files are requested in order
    // and complete in order, so a path is never pending twice and requests
    // are not merged by deduplication.
    function refill() {
        while (queued < total && list.pending() < FILES.length) {
            list.load(FILES[queued++ % FILES.length], {
                upload: config.upload,
                onLoad: (image) => ready.push(image),
                onError: () => errors++,
            });
        }
    }

    refill();
    while (list.pending() > 0 && frames < MAX_FRAMES) {
        const frameStart = System.getMilliseconds();
        list.process(1);
        const processTime = System.getMilliseconds() - frameStart;
        processTotal += processTime;
        processWorst = Math.max(processWorst, processTime);
        // Queue the next request before flip() so the worker can decode it
        // while this frame waits for vblank.
        refill();

        // Draw each image once so "draw" mode pays its upload in the frame,
        // then release it to keep memory flat.
        Screen.clear(0x80000000);
        for (const image of ready) {
            image.draw(0, 0);
        }
        frameWorst = Math.max(frameWorst,
            System.getMilliseconds() - frameStart);
        peakMemory = Math.max(peakMemory, System.getUsedMemory());
        Screen.flip();
        while (ready.length > 0) {
            ready.pop().free();
            loads++;
        }
        frames++;
    }

    const elapsed = System.getMilliseconds() - start;
    const stats = list.stats();
    // Free the worker stack now so the next configuration's peakRam
    // baseline does not depend on when the garbage collector runs.
    list.close();
    console.log(
        `[ImageList bench] ${config.name}: ` +
        `loads=${loads} errors=${errors} frames=${frames} ` +
        `elapsed=${fixed(elapsed)}ms ` +
        `rate=${fixed(loads * 1000 / Math.max(elapsed, 1))}/s ` +
        `process avg=${fixed(processTotal / Math.max(frames, 1))}ms ` +
        `worst=${fixed(processWorst)}ms ` +
        `frameWork worst=${fixed(frameWorst)}ms ` +
        `decode=${fixed(stats.decodeTime)}ms ` +
        `apply=${fixed(stats.applyTime)}ms ` +
        `upload=${fixed(stats.uploadTime)}ms ` +
        `peakBuffered=${stats.peakBufferedBytes}B ` +
        `peakRam=+${peakMemory - baseMemory}B`);
}

for (const config of CONFIGS) {
    run(config);
}
console.log("[ImageList bench] done");
while (true) {
    Screen.clear(0x80182030);
    Screen.flip();
}
