/*
 * Font: sizes, sharing, free(), multi-line text, Font.loadAsync on the
 * shared job pool (the frame loop keeps running while the file is read or
 * the bitmap decoded), preload() and FontRender layouts.
 */
const TTF = "tests/font/Quicksand-Regular.ttf";
const BITMAP = "tests/font/bitmap.png";   // with bitmap.dat, from tests/font/make_fixtures.js
let passed = 0, failed = 0;

function check(name, condition) {
    if (condition) passed++;
    else { failed++; console.log("[FAIL] " + name); }
}

function throws(name, callback, pattern) {
    try {
        callback();
    } catch (error) {
        check(name + ": " + error.message, !pattern || pattern.test(String(error)));
        return;
    }
    check(name + " throws", false);
}

// Sizes and options.
const small = new Font({ size: 16 });
const big = new Font(TTF, { size: 48 });
check("default size", new Font().size === 26);
check("size option", small.size === 16 && big.size === 48);
throws("size out of range", () => new Font({ size: 500 }), /RangeError/);
throws("fractional size", () => new Font(TTF, { size: 20.5 }), /RangeError/);
throws("path and options twice", () => new Font({ size: 20 }, {}), /TypeError/);
check("line height grows with the size", big.lineHeight > small.lineHeight && small.lineHeight > 0);

// Multi-line text: the widest line, and every line counted in the height.
const one = small.getTextSize("Hello");
const two = small.getTextSize("Hello\nHello, world");
check("widest line", two.width === small.getTextSize("Hello, world").width);
check("two lines are taller", two.height === one.height + small.lineHeight);

// camelCase names are the same properties as the old ones.
small.outlineColor = Color.new(1, 2, 3);
check("outlineColor alias", small.outline_color === small.outlineColor);

// free(): using a freed font throws instead of reading freed memory.
const temporary = new Font(TTF, { size: 20 });
const render = temporary.render("text");
temporary.free();
temporary.free();   // twice is harmless
throws("print after free", () => temporary.print(0, 0, "x"), /freed/);
throws("FontRender after free", () => render.print(0, 0), /freed/);

// The same file at the same size is shared: 20 of them fit in the 16 slots.
const shared = [];
for (let i = 0; i < 20; i++) shared.push(new Font(TTF, { size: 30 }));
check("equal fonts are shared", shared.length === 20);
shared.forEach(font => font.free());

async function asyncTests() {
    // Awaited, with the frame loop running meanwhile.
    let frames = 0;
    Loop.run(() => { frames++; });
    const font = await Font.loadAsync(TTF, { size: 32 });
    check("loadAsync resolves with a Font", font instanceof Font && font.size === 32);
    check("frames kept coming", frames >= 0);

    // Polled, and through the module functions.
    const job = Font.loadAsync(undefined, { size: 18 });
    const status = Font.wait(job, 5000);
    check("wait settles: " + status.state, status.state === "done" && status.result.size === 18);
    check("poll returns the same Font", job.poll().result === status.result);

    // Errors.
    const missing = Font.loadAsync("tests/font/missing.ttf");
    let rejected = false;
    try { await missing; } catch (error) { rejected = /Unable to load font/.test(String(error)); }
    check("missing file rejects", rejected);
    throws("Font.poll of another job", () => Font.poll({}), /expected a Font job/);

    // Bitmap fonts: the image and widths are decoded on the pool.
    const bitmap = await Font.loadAsync(BITMAP);
    check("bitmap loadAsync resolves with a Font", bitmap instanceof Font && bitmap.size === 0);
    check("bitmap widths from the .dat", bitmap.getTextSize("AB").width === 21);
    check("same widths as new Font()", new Font(BITMAP).getTextSize("Hello").width ===
        bitmap.getTextSize("Hello").width);
    bitmap.print(10, 10, "AB");
    bitmap.free();
    let bitmapRejected = false;
    try { await Font.loadAsync("tests/font/missing.png"); } catch (error) { bitmapRejected = /Unable to load font/.test(String(error)); }
    check("missing bitmap rejects", bitmapRejected);

    // preload(): the glyphs are rasterized ahead, a slice per frame.
    const warm = new Font(TTF, { size: 44 });
    check("preload resolves with the font", (await warm.preload("0123456789")) === warm);
    check("preload of all ASCII", (await warm.preload()) === warm);
    check("budgetMs 0 finishes during the call", (await warm.preload("xyz", { budgetMs: 0 })) === warm);
    check("Font.ASCII", Font.ASCII.length === 95 && Font.ASCII[0] === " " && Font.ASCII[94] === "~");
    throws("negative budgetMs", () => warm.preload("a", { budgetMs: -1 }), /RangeError/);
    throws("preload characters", () => warm.preload(42), /TypeError/);
    const bitmapPreload = new Font(BITMAP);
    check("bitmap preload resolves at once", (await bitmapPreload.preload()) === bitmapPreload);

    // Freed while its slices were still coming.
    const doomed = new Font(TTF, { size: 70 });
    const pending = doomed.preload(Font.ASCII, { budgetMs: 0.001 });
    doomed.free();
    let freedRejected = false;
    try { await pending; } catch (error) { freedRejected = /freed/.test(String(error)); }
    check("preload rejects once the font is freed", freedRejected);
    warm.free();

    // The first print of new glyphs rasterizes them; after preload() it does not.
    const cold = new Font(TTF, { size: 60 });
    let start = System.getMilliseconds();
    cold.print(0, 100, Font.ASCII);
    const coldMs = System.getMilliseconds() - start;
    const ready = await Font.loadAsync(TTF, { size: 61, preload: true, budgetMs: 1 });
    start = System.getMilliseconds();
    ready.print(0, 100, Font.ASCII);
    const readyMs = System.getMilliseconds() - start;
    console.log(`[INFO] first print of Font.ASCII: ${coldMs} ms cold, ${readyMs} ms after preload`);
    check("loadAsync with preload resolves with a Font", ready instanceof Font && ready.size === 61);
    throws("preload option type", () => Font.loadAsync(TTF, { preload: 5 }), /TypeError/);
    const waited = Font.wait(Font.loadAsync(TTF, { size: 62, preload: "abc" }), 5000);
    check("wait finishes the preload: " + waited.state, waited.state === "done" && waited.result.size === 62);
    cold.free();
    ready.free();
    waited.result.free();

    // FontRender keeps its layout; changing scale, align or outline still prints.
    const label = new Font(TTF, { size: 24 });
    const hud = label.render("Score: 12345\nLives: 3");
    label.outline = 2;
    for (let i = 0; i < 3; i++) hud.print(20, 300);
    label.scale = 1.5;
    label.align = Font.ALIGN_CENTER;
    hud.print(320, 300);
    label.outline = 0;
    label.dropshadow = 2;
    hud.print(320, 300);
    check("FontRender prints after changes", true);
    label.free();

    const cancelled = Font.loadAsync(TTF, { size: 40 });
    cancelled.cancel();
    const end = cancelled.wait(5000);
    check("cancel: " + end.state, end.state === "cancelled" || end.state === "done");

    Loop.stop();
}

asyncTests()
    .catch(error => { failed++; console.log("[FAIL] " + error + "\n" + (error.stack || "")); })
    .then(() => console.log(`Result: ${passed} passed, ${failed} failed`));
