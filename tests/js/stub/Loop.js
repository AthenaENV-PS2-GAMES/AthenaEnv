/*
 * Host stand-in for the Loop module (the real one needs the GS): the systems
 * registry and a run() that plays frames of 1/60 s on setTimeout, in the
 * phase order of src/modules/loop/quickjs/ath_loop.c. Variable step only.
 */
const PHASES = ["preUpdate", "update", "postUpdate", "preDraw", "postDraw"];
const FRAME = 1 / 60;

let systems = [];
let handlers = null;
let generation = 0;
let timeScale = 1;
let delta = 0;
let frames = 0;

export function addSystem(system) {
    if (system === null || typeof system !== "object" || Array.isArray(system))
        throw new TypeError("Loop.addSystem expects an object");
    if (systems.some(entry => entry.object === system))
        throw new TypeError("Loop.addSystem: this system was already added");
    if (system.name !== undefined && systems.some(entry => entry.name === system.name))
        throw new TypeError(`Loop.addSystem: a system named '${system.name}' already exists`);
    const funcs = {};
    for (const phase of PHASES) if (typeof system[phase] === "function") funcs[phase] = system[phase];
    if (!Object.keys(funcs).length) throw new TypeError("Loop.addSystem system needs a phase");
    const entry = { object: system, funcs, name: system.name, priority: system.priority ?? 0,
        realTime: system.realTime ?? false };
    let at = systems.length;
    while (at > 0 && systems[at - 1].priority > entry.priority) at--;
    systems.splice(at, 0, entry);
    return system;
}

export function removeSystem(system) {
    const index = systems.findIndex(entry =>
        typeof system === "string" ? entry.name === system : entry.object === system);
    if (index < 0) return false;
    systems[index].removed = true;
    systems.splice(index, 1);
    return true;
}

export function getSystems() {
    return systems.map(entry => ({ name: entry.name, priority: entry.priority, realTime: entry.realTime,
        phases: PHASES.filter(phase => entry.funcs[phase]), native: false }));
}

function runPhase(phase, value, realValue) {
    for (const entry of systems.slice()) {
        if (entry.removed || !entry.funcs[phase]) continue;
        const real = entry.realTime && (phase === "preUpdate" || phase === "postUpdate");
        entry.funcs[phase].call(entry.object, real ? realValue : value);
    }
}

function frame() {
    const current = generation;
    if (!handlers) return;
    delta = FRAME * timeScale;
    frames++;
    runPhase("preUpdate", delta, FRAME);
    if (current === generation) runPhase("update", delta, delta);
    if (current === generation && handlers.update) handlers.update.call(handlers, delta);
    if (current === generation) runPhase("postUpdate", delta, FRAME);
    if (current === generation) runPhase("preDraw", 1, 1);
    if (current === generation && handlers.draw) handlers.draw.call(handlers, 1);
    if (current === generation) runPhase("postDraw", 1, 1);
    if (handlers) setTimeout(frame, 0);
}

export function run(h) {
    const start = !handlers;
    handlers = typeof h === "function" ? { update: h } : h;
    generation++;
    if (start) setTimeout(frame, 0);
}

export function stop() { handlers = null; generation++; }
export function isRunning() { return !!handlers; }
export function setTimeScale(scale) { timeScale = scale; }
export function getTimeScale() { return timeScale; }
export function getDeltaTime() { return delta; }
export function getFrameCount() { return frames; }
