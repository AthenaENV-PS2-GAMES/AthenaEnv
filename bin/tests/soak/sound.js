// Soak workload (tests/soak_test.js): sound. Music streaming, sound effects
// playing and loadSfxAsync jobs in flight when the VM is torn down.
const FRAMES = 90;

const font = new Font();
const music = Sound.Stream("tests/sound/music.ogg");
music.play();
const pop = new Sound.Sfx("tests/sound/pop.adp");
const jobs = [];

let frames = 0;
Loop.run(() => {
    font.print(40, 40, "Soak: sound, frame " + frames);
    if (frames % 10 === 0) pop.play();
    if (frames % 15 === 0) jobs.push(Sound.loadSfxAsync("tests/sound/pop.adp"));
    if (++frames === FRAMES)
        std.reload("tests/soak_test.js", { returnTo: "tests/index.js" });
});
