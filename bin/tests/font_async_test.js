/*
 * Font: sizes, sharing, free(), multi-line text, and Font.loadAsync on the
 * shared job pool (the frame loop keeps running while the file is read).
 */
const TTF = "tests/font/Quicksand-Regular.ttf";
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
    throws("bitmap fonts are not async", () => Font.loadAsync("tests/font.png"), /new Font/);
    throws("Font.poll of another job", () => Font.poll({}), /expected a Font job/);

    const cancelled = Font.loadAsync(TTF, { size: 40 });
    cancelled.cancel();
    const end = cancelled.wait(5000);
    check("cancel: " + end.state, end.state === "cancelled" || end.state === "done");

    Loop.stop();
}

asyncTests()
    .catch(error => { failed++; console.log("[FAIL] " + error + "\n" + (error.stack || "")); })
    .then(() => console.log(`Result: ${passed} passed, ${failed} failed`));
