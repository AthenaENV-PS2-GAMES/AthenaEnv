function check(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function expectThrow(callback, type, message) {
    let error;
    try {
        callback();
    } catch (caught) {
        error = caught;
    }
    check(error instanceof type, message);
}

// The worker only runs while the main thread blocks; Screen.flip() blocks on
// the vblank interrupt, which gives it CPU time like a real frame loop does.
function drain(list, budget) {
    let frames = 0;
    while (list.pending() > 0 && frames++ < 1000) {
        list.process(budget);
        Screen.flip();
    }
    check(list.pending() === 0, "ImageList did not finish its pending requests");
}

// Argument validation.
expectThrow(() => new ImageList([]), TypeError,
    "ImageList accepted non-object options");
expectThrow(() => new ImageList({ workers: 2 }), RangeError,
    "ImageList accepted more than one worker");
expectThrow(() => new ImageList({ maxMemory: -1 }), RangeError,
    "ImageList accepted a negative maxMemory");
const validation = new ImageList();
expectThrow(() => validation.load(42), TypeError,
    "ImageList.load accepted a non-string path");
expectThrow(() => validation.load("tests/my_image.png", { priority: "high" }),
    RangeError, "ImageList.load accepted a non-numeric priority");
expectThrow(() => validation.load("tests/my_image.png", { priority: 3 }),
    RangeError, "ImageList.load accepted an unknown priority");
expectThrow(() => validation.load("tests/my_image.png", { onLoad: 1 }),
    TypeError, "ImageList.load accepted a non-function onLoad");
expectThrow(() => validation.cancel(1), TypeError,
    "ImageList.cancel accepted a non-object");
check(validation.pending() === 0, "Rejected ImageList requests were queued");

// Cooperative loading and callbacks.
const list = new ImageList();
let loaded = false;
let failed = false;
let loadedImage;

const image = list.load("tests/my_image.png", {
    onLoad: (result) => {
        loaded = true;
        loadedImage = result;
    },
    onError: () => {
        failed = true;
    },
});

check(image.loading() && image.status() === "queued" && !image.ready(),
    "ImageList image was not queued");
check(list.stats().queued === 1 && list.stats().loading === 0,
    "ImageList cooperative request was not reported as queued");

while (list.pending() > 0) {
    check(list.process() === 1, "ImageList.process did not process one image");
}

check(!failed, "ImageList reported an unexpected load failure");
check(loaded && loadedImage === image,
    "ImageList onLoad callback was not dispatched");
check(image.ready() && !image.loading(), "ImageList onLoad image is not ready");
check(image.status() === "decoded",
    "ImageList image did not enter the decoded state");

// Structured errors.
let errorImage;
let loadError;
const missing = list.load("host:/missing-image.png", {
    onError: (result, error) => {
        errorImage = result;
        loadError = error;
    },
});

list.process();
check(missing.failed() && !missing.ready() &&
    missing.status() === "failed" && errorImage === missing &&
    loadError && loadError.path === "host:/missing-image.png" &&
    loadError.code === "open_failed" && loadError.stage === "open" &&
    typeof loadError.message === "string" && loadError.message.length > 0 &&
    missing.error().path === loadError.path &&
    missing.error().code === loadError.code,
    "ImageList onError callback was not dispatched correctly");
check(image.error() === undefined,
    "A successfully loaded image reported an error");

// Individual cancellation and clear().
let cancelledCallback = false;
const cancelled = list.load("tests/my_image.png", {
    onLoad: () => {
        cancelledCallback = true;
    },
    onError: () => {
        cancelledCallback = true;
    },
});
check(list.cancel(cancelled) && !list.cancel(cancelled) &&
    cancelled.status() === "cancelled" && !cancelled.loading() &&
    !cancelled.failed() && !cancelledCallback && list.pending() === 0,
    "ImageList request cancellation was not handled correctly");
check(!list.cancel(image), "ImageList cancelled an already completed request");

const cleared = list.load("tests/my_image.png");
list.clear();
check(cleared.status() === "cancelled" && !cleared.failed() &&
    !cleared.loading() && list.pending() === 0,
    "ImageList.clear did not cancel the queued request");

const stats = list.stats();
check(stats.queued === 0 && stats.loading === 0 && stats.completed === 1 &&
    stats.failed === 1 && stats.cancelled === 2 && stats.workers === 0 &&
    stats.bufferedBytes === 0 && stats.peakBufferedBytes > 0,
    "ImageList.stats returned unexpected counters");

// Deduplication of pending requests by normalized path.
const dedupList = new ImageList();
let dedupLoads = 0;
const dedupFirst = dedupList.load("tests/./my_image.png", {
    onLoad: () => {
        dedupLoads++;
    },
});
const dedupSecond = dedupList.load("tests//../tests/my_image.png", {
    onLoad: () => {
        dedupLoads++;
    },
});
check(dedupFirst === dedupSecond && dedupList.pending() === 1,
    "ImageList did not deduplicate normalized pending paths");
dedupList.process();
check(dedupLoads === 2 && dedupFirst.ready(),
    "ImageList did not dispatch deduplicated callbacks");

const normalizeList = new ImageList();
check(normalizeList.load("host:/dir/../same.png") ===
    normalizeList.load("host:/same.png"),
    "ImageList did not normalize paths with a device prefix");
check(normalizeList.load("host:/../root.png") ===
    normalizeList.load("host:/root.png"),
    "ImageList climbed above a device root");
check(normalizeList.load("../outside.png") !==
    normalizeList.load("outside.png"),
    "ImageList dropped a leading '..' from a relative path");
check(normalizeList.pending() === 4,
    "ImageList normalization produced unexpected requests");
normalizeList.clear();

// A freed Image must not be used by its pending request.
const freeList = new ImageList();
let freedCallback = false;
const freed = freeList.load("tests/my_image.png", {
    onLoad: () => {
        freedCallback = true;
    },
});
freed.free();
const replacement = freeList.load("tests/my_image.png");
check(replacement !== freed && freeList.pending() === 1,
    "ImageList reused a freed Image for a new request");
check(freeList.process() === 1 && replacement.ready() && !freedCallback,
    "ImageList did not drop the request of a freed Image");
check(freeList.stats().cancelled === 1,
    "ImageList did not count the freed request as cancelled");

// Callback exceptions propagate without corrupting the queue.
const throwList = new ImageList();
const throwing = throwList.load("tests/my_image.png", {
    onLoad: () => {
        throw new Error("callback failure");
    },
});
expectThrow(() => throwList.process(), Error,
    "ImageList swallowed a callback exception");
check(throwList.pending() === 0 && throwing.ready(),
    "ImageList state was inconsistent after a callback exception");

// Stable priority ordering and promotion of duplicates.
const priorityList = new ImageList();
const priorityOrder = [];
priorityList.load("host:/normal.png", {
    onError: () => priorityOrder.push("normal"),
});
priorityList.load("host:/normal-2.png", {
    onError: () => priorityOrder.push("normal-2"),
});
priorityList.load("host:/low.png", {
    priority: ImageList.LOW,
    onError: () => priorityOrder.push("low"),
});
priorityList.load("host:/high.png", {
    priority: ImageList.HIGH,
    onError: () => priorityOrder.push("high"),
});
priorityList.load("host:/promoted.png", {
    priority: ImageList.LOW,
    onError: () => priorityOrder.push("promoted"),
});
priorityList.load("host:/promoted.png", { priority: ImageList.HIGH });
priorityList.process(5);
check(priorityOrder.join(",") === "high,promoted,normal,normal-2,low",
    "ImageList priority ordering was not stable");

// Object budgets.
const budgetList = new ImageList();
budgetList.load("tests/my_image.png");
budgetList.load("host:/budget-missing.png");
check(budgetList.process({ maxItems: 2, maxBytes: 1 }) === 1 &&
    budgetList.pending() === 1,
    "ImageList process maxBytes budget was not honored");
check(budgetList.process({ maxItems: 0 }) === 0 && budgetList.pending() === 1,
    "ImageList process maxItems budget was not honored");
budgetList.clear();
expectThrow(() => budgetList.process({ maxTime: -1 }), RangeError,
    "ImageList.process accepted a negative maxTime");

// Time budget: the first request always completes, then the clock is checked.
const timeList = new ImageList();
timeList.load("tests/my_image.png", {
    onLoad: () => {
        // Spin past the 1 ms budget; the iteration cap guards a coarse clock.
        const start = Date.now();
        for (let i = 0; Date.now() - start < 5 && i < 200000; i++) {}
    },
});
timeList.load("host:/time-missing.png");
check(timeList.process({ maxItems: 2, maxTime: 1 }) === 1 &&
    timeList.pending() === 1,
    "ImageList process maxTime budget was not honored");
check(timeList.process({ maxItems: 2, maxTime: 0 }) === 1 &&
    timeList.pending() === 0,
    "ImageList process maxTime of zero limited the call");
const timeStats = timeList.stats();
check(typeof timeStats.decodeTime === "number" && timeStats.decodeTime >= 0 &&
    typeof timeStats.applyTime === "number" && timeStats.applyTime >= 0,
    "ImageList.stats did not report decode and apply times");

// Cache of completed images.
expectThrow(() => new ImageList({ cacheSize: 1025 }), RangeError,
    "ImageList accepted an oversized cacheSize");
const cacheList = new ImageList({ cacheSize: 1 });
const cachedFirst = cacheList.load("tests/my_image.png");
cacheList.process();
check(cacheList.stats().cached === 1, "ImageList did not cache a loaded image");
let cacheHit;
const cachedAgain = cacheList.load("tests/./my_image.png", {
    onLoad: (result) => {
        cacheHit = result;
    },
});
check(cachedAgain === cachedFirst && cacheList.pending() === 1 &&
    cachedAgain.status() === "decoded" && cacheHit === undefined,
    "ImageList did not return the cached Image");
check(cacheList.process() === 1 && cacheHit === cachedFirst &&
    cacheList.stats().cacheHits === 1 && cacheList.stats().completed === 2,
    "ImageList did not dispatch a cache hit inside process()");
const cancelledHit = cacheList.load("tests/my_image.png", {
    onLoad: () => {
        cacheHit = null;
    },
});
check(cacheList.cancel(cancelledHit) && cancelledHit.status() === "decoded" &&
    cancelledHit.ready() && cacheHit === cachedFirst,
    "Cancelling a cache hit changed the cached Image");
cacheList.load("host:/cache-missing.png");
cacheList.process();
check(cacheList.stats().cached === 1,
    "ImageList cached a failed image");
// A second valid image; host: exists only on PCSX2, not on a real console.
const cachedOther = cacheList.load("tests/texture.png");
cacheList.process();
const evicted = cacheList.load("tests/my_image.png");
check(evicted !== cachedFirst && evicted.status() === "queued" &&
    cachedOther.ready(),
    "ImageList did not evict the least recently used image");
cacheList.clear();
check(cacheList.clearCache() === 1 && cacheList.stats().cached === 0,
    "ImageList.clearCache did not release cached images");
const freedCache = new ImageList({ cacheSize: 2 });
const freedCached = freedCache.load("tests/my_image.png");
freedCache.process();
freedCached.free();
const reloaded = freedCache.load("tests/my_image.png");
check(reloaded !== freedCached && reloaded.status() === "queued" &&
    freedCache.stats().cached === 0,
    "ImageList returned a freed cached Image");
freedCache.clear();

// Worker mode: decoding happens on the worker, completion in process().
const workerList = new ImageList({ workers: 1 });
check(workerList.stats().workers === 1, "ImageList worker did not start");
let workerLoaded = 0;
let workerError;
const workerImage = workerList.load("tests/my_image.png", {
    onLoad: () => workerLoaded++,
});
const workerDuplicate = workerList.load("tests/./my_image.png", {
    onLoad: () => workerLoaded++,
});
const workerMissing = workerList.load("host:/worker-missing.png", {
    onError: (result, error) => {
        workerError = error;
    },
});
check(workerDuplicate === workerImage && workerList.pending() === 2,
    "ImageList worker mode did not deduplicate pending paths");
check(workerImage.status() === "loading" && workerMissing.status() === "queued",
    "ImageList worker did not start the first request immediately");
check(workerList.stats().loading === 1 && workerList.stats().queued === 1,
    "ImageList without maxMemory submitted more than one request");
drain(workerList, 1);
check(workerImage.ready() && workerImage.status() === "decoded" &&
    workerLoaded === 2,
    "ImageList worker mode did not complete the image");
check(workerMissing.failed() && workerError &&
    workerError.code === "open_failed" &&
    workerError.path === "host:/worker-missing.png",
    "ImageList worker mode did not report structured errors");
const workerStats = workerList.stats();
check(workerStats.completed === 1 && workerStats.failed === 1 &&
    workerStats.bufferedBytes === 0 && workerStats.peakBufferedBytes > 0,
    "ImageList worker stats were not updated");

// Worker mode: requests submitted before a more urgent one keep running.
const workerOrder = [];
const workerPriority = new ImageList({ workers: 1 });
workerPriority.load("host:/worker-first.png", {
    onError: () => workerOrder.push("first"),
});
workerPriority.load("host:/worker-low.png", {
    priority: ImageList.LOW,
    onError: () => workerOrder.push("low"),
});
workerPriority.load("host:/worker-high.png", {
    priority: ImageList.HIGH,
    onError: () => workerOrder.push("high"),
});
drain(workerPriority, 1);
check(workerOrder.join(",") === "first,high,low",
    "ImageList worker mode did not respect priorities");

// Worker mode: cancelling a request that is already being decoded.
const workerCancel = new ImageList({ workers: 1 });
let workerCancelledCallback = false;
const inFlight = workerCancel.load("tests/my_image.png", {
    onLoad: () => {
        workerCancelledCallback = true;
    },
});
const afterCancel = workerCancel.load("host:/after-cancel.png");
check(workerCancel.cancel(inFlight) && inFlight.status() === "cancelled" &&
    workerCancel.pending() === 1,
    "ImageList worker mode did not cancel an in-flight request");
drain(workerCancel, 4);
check(!workerCancelledCallback && inFlight.status() === "cancelled" &&
    afterCancel.failed() && workerCancel.stats().cancelled === 1,
    "ImageList worker mode dispatched a cancelled request");
const clearedInFlight = workerCancel.load("tests/my_image.png");
workerCancel.clear();
check(clearedInFlight.status() === "cancelled" && workerCancel.pending() === 0,
    "ImageList.clear did not cancel an in-flight request");

// Worker mode: maxMemory lets the worker decode ahead.
const aheadList = new ImageList({ workers: 1, maxMemory: 1024 * 1024 });
const ahead = [
    aheadList.load("tests/my_image.png"),
    aheadList.load("host:/ahead-1.png"),
    aheadList.load("host:/ahead-2.png"),
];
check(aheadList.stats().loading === 3,
    "ImageList maxMemory did not allow decode-ahead");
drain(aheadList, 4);
check(ahead[0].ready() && ahead[1].failed() && ahead[2].failed(),
    "ImageList decode-ahead requests did not complete");

// Upload stage inside process().
expectThrow(() => validation.load("tests/my_image.png", { upload: "now" }),
    RangeError, "ImageList.load accepted an unknown upload mode");
const uploadList = new ImageList();
let boundStatus;
const bound = uploadList.load("tests/my_image.png", {
    upload: "bind",
    onLoad: (result) => {
        boundStatus = result.status();
    },
});
uploadList.process();
check(boundStatus === "ready" && bound.ready() && !bound.locked(),
    "ImageList upload \"bind\" did not make the image resident");
let lockedStatus;
const locked = uploadList.load("tests/texture.png", {
    upload: "lock",
    onLoad: (result) => {
        lockedStatus = result.status();
    },
});
// A weaker duplicate must not downgrade the requested upload mode.
uploadList.load("tests/texture.png", { upload: "draw" });
uploadList.process();
check(lockedStatus === "ready" && locked.locked(),
    "ImageList upload \"lock\" did not lock the image");
locked.unlock();
const uploadStats = uploadList.stats();
check(uploadStats.completed === 2 && typeof uploadStats.uploadTime === "number" &&
    uploadStats.uploadTime >= 0,
    "ImageList.stats did not report upload time");

// close() releases the worker, pending requests and cache immediately.
const closeList = new ImageList({ workers: 1, cacheSize: 2 });
const closeCached = closeList.load("tests/my_image.png");
drain(closeList, 1);
const closePending = closeList.load("host:/close-pending.png");
check(closeList.stats().workers === 1 && closeList.stats().cached === 1,
    "ImageList close test did not start from a running worker and cache");
closeList.close();
const closeStats = closeList.stats();
check(closePending.status() === "cancelled" && closeList.pending() === 0 &&
    closeStats.workers === 0 && closeStats.cached === 0 &&
    closeStats.cancelled === 1 && closeCached.ready(),
    "ImageList.close did not release its resources");
expectThrow(() => closeList.load("tests/my_image.png"), TypeError,
    "ImageList.load accepted a request after close()");
closeList.close();
check(closeList.process() === 0 && closeList.clearCache() === 0,
    "ImageList was not inert after close()");

// Destroying a list joins its worker and cancels pending requests.
let orphan;
(function () {
    const temporary = new ImageList({ workers: 1 });
    orphan = temporary.load("tests/my_image.png");
})();
check(orphan.status() === "cancelled",
    "Destroying an ImageList did not cancel its pending request");

check(image.lock() && image.status() === "ready",
    "ImageList image did not complete its upload");
console.log("ImageList tests passed");
while (true) {
    Screen.clear(0x80182030);
    image.draw(
        (Screen.getMode().width - image.width) / 2,
        (Screen.getMode().height - image.height) / 2
    );
    Screen.flip();
}
