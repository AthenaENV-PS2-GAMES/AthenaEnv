/*
 * Input: named actions and axes over controller snapshots.
 *
 * A Map turns the snapshot of a source (a Gamepad.Player by default, or any
 * object with buttons, leftX, leftY, rightX and rightY, such as a Replay
 * source) into actions: buttons with pressed/justPressed/justReleased,
 * digital axes from two buttons, and sticks with dead zone, response curve,
 * sensitivity, inversion and an optional d-pad fallback. Edges are tracked
 * by the Map itself, so they follow map.update() and work with any source.
 *
 * Bindings are plain data (bindings()/load()), so they can be saved with the
 * game's settings and rebound in an options menu.
 */
const g = globalThis;

const STICKS = { left: ["leftX", "leftY"], right: ["rightX", "rightY"] };
const CURVES = { linear: 1, quadratic: 2, cubic: 3 };
const UP = 0x0010, RIGHT = 0x0020, DOWN = 0x0040, LEFT = 0x0080;

function checkMask(mask, where) {
    if (!Number.isInteger(mask) || mask <= 0 || mask > 0xFFFF)
        throw new TypeError(`${where}: buttons must be Gamepad button bits (1 to 0xFFFF)`);
    return mask;
}

/** A button action: held when every bit of any of the masks is held (L1 | R1 is a combination). */
export function button(...masks) {
    if (!masks.length) throw new TypeError("Input.button needs at least one button mask");
    return { button: masks.map(m => checkMask(m, "Input.button")) };
}

/** A digital axis: -1 while `negative` is held, +1 while `positive` is held, 0 for both or none. */
export function axis(negative, positive) {
    return { axis: [checkMask(negative, "Input.axis"), checkMask(positive, "Input.axis")] };
}

/**
 * A stick: "left" or "right". Options: deadZone (radial, 0..0.95, default
 * 0.15), curve ("linear" default, "quadratic", "cubic"), sensitivity
 * (default 1), invertX, invertY, dpad (use the d-pad while the stick rests).
 */
export function stick(side, options = {}) {
    return normalize({ stick: side, ...options }, "Input.stick");
}

function normalize(binding, where) {
    if (binding === null || typeof binding !== "object") throw new TypeError(`${where}: a binding must be an object`);
    if (binding.button !== undefined) {
        const masks = Array.isArray(binding.button) ? binding.button : [binding.button];
        if (!masks.length) throw new TypeError(`${where}: button needs at least one mask`);
        return { button: masks.map(m => checkMask(m, where)) };
    }
    if (binding.axis !== undefined) {
        if (!Array.isArray(binding.axis) || binding.axis.length !== 2) throw new TypeError(`${where}: axis must be [negative, positive]`);
        return { axis: [checkMask(binding.axis[0], where), checkMask(binding.axis[1], where)] };
    }
    if (binding.stick !== undefined) {
        if (!STICKS[binding.stick]) throw new TypeError(`${where}: stick must be "left" or "right"`);
        const out = { stick: binding.stick, deadZone: 0.15, curve: "linear", sensitivity: 1,
            invertX: false, invertY: false, dpad: false };
        if (binding.deadZone !== undefined) {
            if (typeof binding.deadZone !== "number" || !(binding.deadZone >= 0 && binding.deadZone <= 0.95))
                throw new RangeError(`${where}: deadZone must be from 0 to 0.95`);
            out.deadZone = binding.deadZone;
        }
        if (binding.curve !== undefined) {
            if (!CURVES[binding.curve]) throw new RangeError(`${where}: curve must be "linear", "quadratic" or "cubic"`);
            out.curve = binding.curve;
        }
        if (binding.sensitivity !== undefined) {
            if (typeof binding.sensitivity !== "number" || !Number.isFinite(binding.sensitivity))
                throw new RangeError(`${where}: sensitivity must be a finite number`);
            out.sensitivity = binding.sensitivity;
        }
        for (const flag of ["invertX", "invertY", "dpad"]) if (binding[flag] !== undefined) out[flag] = !!binding[flag];
        return out;
    }
    throw new TypeError(`${where}: a binding needs button, axis or stick`);
}

/* Per action runtime state. kind: 0 button, 1 axis, 2 stick (cached from
 * the binding so update() runs without property lookups on it). */
function makeState(binding) {
    const state = { binding, kind: 0, masks: null, neg: 0, pos: 0, held: false, was: false, frames: 0, value: 0,
        vector: { x: 0, y: 0 } };
    compile(state);
    return state;
}
function compile(state) {
    const b = state.binding;
    state.kind = b.button ? 0 : b.axis ? 1 : 2;
    state.masks = b.button || null;
    if (b.axis) { state.neg = b.axis[0]; state.pos = b.axis[1]; }
}

export class Map {
    /**
     * bindings: { name: binding }. options.player: Gamepad player index
     * (default 0); options.source: any snapshot source instead.
     */
    constructor(bindings, options = {}) {
        if (bindings === null || typeof bindings !== "object") throw new TypeError("Input.Map expects an object of bindings");
        this._actions = new g.Map();
        this._list = [];
        this.raw = { buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0 };
        this._source = null;
        if (options.source !== undefined) this.setSource(options.source);
        else this._player = options.player ?? 0;
        this.load(bindings);
    }

    /** Reads snapshots from `source` (a Gamepad.Player, a Replay source, a test double); null: the Gamepad player. */
    setSource(source) {
        if (source !== null && (typeof source !== "object" || typeof source.buttons !== "number"))
            throw new TypeError("Input.Map source must have numeric buttons, leftX, leftY, rightX and rightY");
        this._source = source;
        return this;
    }

    get source() {
        if (this._source) return this._source;
        const Gamepad = g.Gamepad;
        if (!Gamepad) throw new Error("Input.Map needs the Gamepad module or a source");
        const player = Gamepad.player(this._player);
        /* The map applies each stick's own dead zone to raw values. */
        if (player.deadzone !== 0) player.deadzone = 0;
        this._source = player;
        return player;
    }

    /** Replaces every binding (actions not named are removed); state restarts. */
    load(bindings) {
        const next = new g.Map();
        for (const name of Object.keys(bindings)) next.set(name, makeState(normalize(bindings[name], `Input binding "${name}"`)));
        this._actions = next;
        this._list = [...next.values()];
        return this;
    }

    /** Plain copy of the bindings, for saving with the settings. */
    bindings() {
        const out = {};
        for (const [name, state] of this._actions) out[name] = JSON.parse(JSON.stringify(state.binding));
        return out;
    }

    /** Changes one action's binding: a binding object or button masks. */
    rebind(name, ...binding) {
        const state = this._state(name);
        const value = binding.length === 1 && typeof binding[0] === "object" ? binding[0] : { button: binding };
        state.binding = normalize(value, `Input.rebind("${name}")`);
        compile(state);
        state.held = state.was = false; state.frames = 0;
        return this;
    }

    has(name) { return this._actions.has(name); }

    _state(name) {
        const state = this._actions.get(name);
        if (!state) throw new RangeError(`Input: unknown action "${name}"`);
        return state;
    }

    /** Takes the source's snapshot and updates every action. Call once per frame (after Gamepad.update()). */
    update() {
        const s = this._source || this.source, raw = this.raw;
        const buttons = s.buttons | 0;
        raw.buttons = buttons; raw.leftX = s.leftX; raw.leftY = s.leftY; raw.rightX = s.rightX; raw.rightY = s.rightY;
        const list = this._list;
        for (let i = 0; i < list.length; i++) {
            const state = list[i], was = state.held;
            let held;
            state.was = was;
            if (state.kind === 0) {
                const masks = state.masks;
                held = false;
                for (let k = 0; k < masks.length; k++) { const m = masks[k]; if ((buttons & m) === m) { held = true; break; } }
                state.value = held ? 1 : 0;
            } else if (state.kind === 1) {
                const v = ((buttons & state.pos) === state.pos ? 1 : 0) - ((buttons & state.neg) === state.neg ? 1 : 0);
                state.value = v; held = v !== 0;
            } else held = this._stick(state, state.binding, raw, buttons);
            state.held = held;
            state.frames = held ? (was ? state.frames + 1 : 1) : 0;
        }
        return this;
    }

    _stick(state, b, raw, buttons) {
        const left = b.stick === "left";
        let x = left ? raw.leftX : raw.rightX, y = left ? raw.leftY : raw.rightY, out = 0;
        if (!(x === x)) x = 0;          /* NaN from an odd source */
        if (!(y === y)) y = 0;
        const m2 = x * x + y * y, dz = b.deadZone;
        if (m2 > dz * dz) {
            const m = Math.sqrt(m2), scaled = ((m < 1 ? m : 1) - dz) / (1 - dz), curve = b.curve;
            out = (curve === "linear" ? scaled : curve === "quadratic" ? scaled * scaled : scaled * scaled * scaled) * b.sensitivity;
            const k = out / m; x *= k; y *= k;
        } else if (b.dpad && (buttons & 0xF0)) {
            x = ((buttons & RIGHT) ? 1 : 0) - ((buttons & LEFT) ? 1 : 0);
            y = ((buttons & DOWN) ? 1 : 0) - ((buttons & UP) ? 1 : 0);
            if (x || y) {
                out = b.sensitivity;
                const k = x && y ? out * Math.SQRT1_2 : out;
                x *= k; y *= k;
            }
        } else { x = 0; y = 0; }
        const v = state.vector;
        v.x = b.invertX ? -x : x; v.y = b.invertY ? -y : y;
        state.value = out < 0 ? -out : out;
        return out !== 0;
    }

    /** Held (buttons), nonzero (axes), outside the dead zone (sticks). */
    pressed(name) { return this._state(name).held; }
    justPressed(name) { const s = this._state(name); return s.held && !s.was; }
    justReleased(name) { const s = this._state(name); return !s.held && s.was; }
    /** Consecutive updates the action has been held (0 when released). */
    heldFrames(name) { return this._state(name).frames; }
    /** 0/1 for buttons, -1/0/1 for axes, the processed magnitude for sticks. */
    value(name) { return this._state(name).value; }
    /** A stick's processed vector; the same object every call, updated by update(). */
    axis(name) {
        const s = this._state(name);
        if (!s.binding.stick) throw new TypeError(`Input: "${name}" is not a stick`);
        return s.vector;
    }
    x(name) { return this.axis(name).x; }
    y(name) { return this.axis(name).y; }
    /** Action names. */
    actions() { return [...this._actions.keys()]; }
}

/**
 * Presets of common layouts (Gamepad bit values): "platformer", "shooter"
 * (twin-stick / first person) and "menu". Returns new binding objects.
 */
export function preset(name) {
    const CROSS = 0x4000, CIRCLE = 0x2000, SQUARE = 0x8000, TRIANGLE = 0x1000;
    const L1 = 0x0400, R1 = 0x0800, L2 = 0x0100, R2 = 0x0200, START = 0x0008, SELECT = 0x0001;
    switch (name) {
        case "platformer": return {
            move: stick("left", { dpad: true }), jump: button(CROSS), attack: button(SQUARE),
            run: button(R1), pause: button(START),
        };
        case "shooter": return {
            move: stick("left"), look: stick("right", { curve: "quadratic", sensitivity: 2.5 }),
            fire: button(R2), aim: button(L2), jump: button(CROSS), reload: button(SQUARE),
            interact: button(TRIANGLE), crouch: button(CIRCLE), pause: button(START),
        };
        case "menu": return {
            vertical: axis(UP, DOWN), horizontal: axis(LEFT, RIGHT), confirm: button(CROSS),
            back: button(CIRCLE), pageLeft: button(L1), pageRight: button(R1), options: button(SELECT),
        };
        default: throw new RangeError(`Input.preset: unknown preset "${name}"`);
    }
}
