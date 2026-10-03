// Soak workload (tests/soak_test.js): fonts. TrueType and bitmap loads in
// flight, glyphs being preloaded and FontRenders when the VM is torn down.
const FRAMES = 90;
const TTF = "tests/font/Quicksand-Regular.ttf";

const base = new Font(TTF, { size: 30 });
base.outline = 1;
const label = base.render("Soak: fonts\nloads, preloads, renders");
base.preload(Font.ASCII, { budgetMs: 0.5 });
const jobs = [
    Font.loadAsync(TTF, { size: 40, preload: true, budgetMs: 1 }),
    Font.loadAsync("tests/font/bitmap.png"),
];
const loaded = [];
jobs.forEach(job => job.then(font => loaded.push(font)));

let frames = 0;
Loop.run(() => {
    label.print(40, 40);
    base.print(40, 120, "frame " + frames + ", fonts " + loaded.length);
    loaded.forEach((font, i) => font.print(40, 160 + 40 * i, "Loaded " + i));
    if (frames === 30)
        jobs.push(Font.loadAsync(TTF, { size: 90, preload: true, budgetMs: 0.2 }));   // still busy at the switch
    if (++frames === FRAMES)
        std.reload("tests/soak_test.js", { returnTo: "tests/index.js" });
});
