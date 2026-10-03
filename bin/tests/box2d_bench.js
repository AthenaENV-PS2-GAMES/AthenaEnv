// Box2D benchmark: time per phase of the step (World.getProfile, measured
// with the EE cycle counter) for growing scenes, and the cost of reading
// transforms one object at a time versus World.readTransforms.
//
// Use a release build (asserts off) on real hardware for figures that
// matter; PCSX2 timings are only relative. Output goes to the console.

const FRAMES = 240;

function scene(boxes) {
    const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });
    const ground = world.createBody({ position: { x: 0, y: -1 } });
    ground.createBoxShape({ halfWidth: 60, halfHeight: 1 });
    const bodies = [];
    const perRow = Math.ceil(Math.sqrt(boxes));
    for (let i = 0; i < boxes; i++) {
        const body = world.createBody({ type: Box2D.DYNAMIC_BODY,
            position: { x: (i % perRow) * 1.1 - perRow * 0.55, y: 0.5 + Math.floor(i / perRow) * 1.05 } });
        body.createBoxShape({ halfWidth: 0.5, halfHeight: 0.5 });
        bodies.push(body);
    }
    return { world: world, bodies: bodies };
}

function measureStep(boxes) {
    const s = scene(boxes);
    const phases = { step: 0, collide: 0, solve: 0, solveImpulses: 0, pairs: 0 };
    let worst = 0;
    for (let frame = 0; frame < FRAMES; frame++) {
        s.world.step(1 / 60, 4);
        const profile = s.world.getProfile();
        for (const name in phases) phases[name] += profile[name];
        if (profile.step > worst) worst = profile.step;
    }
    let line = boxes + " boxes:";
    for (const name in phases) line += " " + name + " " + (phases[name] / FRAMES).toFixed(3);
    console.log(line + " ms (worst step " + worst.toFixed(3) + " ms)");
    s.world.destroy();
}

function measureReads(boxes) {
    const s = scene(boxes);
    const out = new Float32Array(boxes * 3);
    const rounds = 60;

    let start = Date.now();
    for (let r = 0; r < rounds; r++)
        for (let i = 0; i < boxes; i++) s.bodies[i].getTransform();
    const objects = (Date.now() - start) / rounds;

    start = Date.now();
    for (let r = 0; r < rounds; r++) s.world.readTransforms(s.bodies, out);
    const batch = (Date.now() - start) / rounds;

    console.log(boxes + " bodies per frame: getTransform() " + objects.toFixed(3) +
        " ms, readTransforms() " + batch.toFixed(3) + " ms");
    s.world.destroy();
}

[25, 100, 200].forEach(measureStep);
[100, 200].forEach(measureReads);
console.log("Box2D benchmark finished");
