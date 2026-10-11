/**
 * Named actions and axes over controller snapshots.
 *
 * A Map turns the snapshot of a source (Gamepad player 0 by default, or any
 * object with `buttons`, `leftX`, `leftY`, `rightX`, `rightY` such as a
 * Replay source) into actions. Edges (justPressed/justReleased) are tracked
 * by the map on each `update()`, so they work with any source. Sticks get
 * their own radial dead zone, so a Map sets its Gamepad player's
 * `deadzone` to 0 and reads raw values.
 *
 * `Input.pressure(Gamepad.R2, { threshold: 0.3 })` binds the analog pressure of a button (DualShock 2/3
 * face, shoulder and d-pad buttons, DualShock 4 L2/R2); `value()` gives 0..1 and sources without pressure
 * fall back to the digital bit.
 *
 * Bindings are plain data: save `map.bindings()` with the settings and
 * restore them with `map.load()`.
 *
 * Not in the default build: `node tools/modules.js configure --modules=input,...`
 *
 * Example:
 * ```js
 * const controls = new Input.Map({
 *     move: Input.stick("left", { dpad: true }),
 *     look: Input.stick("right", { curve: "quadratic", sensitivity: 2.5 }),
 *     jump: Input.button(Gamepad.CROSS),
 *     dash: Input.button(Gamepad.L1 | Gamepad.R1),   // both together
 * });
 * Loop.run({ update(dt) {
 *     Gamepad.update(); controls.update();
 *     const m = controls.axis("move");             // same object every frame
 *     player.x += m.x * speed * dt;
 *     if (controls.justPressed("jump")) player.jump();
 * } });
 * controls.rebind("jump", Gamepad.CIRCLE);
 * ```
 */
declare namespace Input {
    interface ButtonBinding { button: number[] }
    interface AxisBinding { axis: [number, number] }
    interface StickBinding {
        stick: "left" | "right";
        /** Radial, 0 to 0.95 (default 0.15); the rest is rescaled to 0..1. */
        deadZone?: number;
        curve?: "linear" | "quadratic" | "cubic";
        sensitivity?: number;
        invertX?: boolean;
        invertY?: boolean;
        /** Use the d-pad while the stick rests in its dead zone. */
        dpad?: boolean;
    }
    interface PressureBinding {
        /** One Gamepad button bit. */
        pressure: number;
        /** Held from this pressure, in (0, 1] (default 0.5). */
        threshold: number;
        /** Stays held until the pressure falls below this, in (0, threshold] (default threshold). */
        release: number;
    }
    type Binding = ButtonBinding | AxisBinding | StickBinding | PressureBinding;
    /** What a Map reads; Gamepad.Player fits. */
    interface Source {
        buttons: number; leftX: number; leftY: number; rightX: number; rightY: number;
        /** Optional: how hard one button bit is pressed, 0..1. Without it pressure actions read the digital bit (1 while held). */
        pressure?(button: number): number;
    }

    /** Held while every bit of any mask is held (`L1 | R1` is a combination; several masks are alternatives). */
    function button(...masks: number[]): ButtonBinding;
    /** -1 while `negative` is held, +1 while `positive` is held. */
    function axis(negative: number, positive: number): AxisBinding;
    /**
     * Pressure of one button (value() is 0..1), held from `threshold` until it falls below `release`.
     * Sources or buttons without pressure give 1 while held, so the action works digitally.
     */
    function pressure(button: number, options?: { threshold?: number; release?: number }): PressureBinding;
    function stick(side: "left" | "right", options?: Omit<StickBinding, "stick">): StickBinding;
    /** New bindings of a common layout. */
    function preset(name: "platformer" | "shooter" | "menu"): Record<string, Binding>;

    class Map {
        constructor(bindings: Record<string, Binding>, options?: { player?: number; source?: Source });
        /** The last snapshot read by update(). */
        readonly raw: Source;
        /** The source read by update() (the Gamepad player unless setSource() was used). */
        readonly source: Source;
        /** Reads another source; null goes back to the Gamepad player. */
        setSource(source: Source | null): this;
        /** Takes the snapshot and updates every action; once per frame, after Gamepad.update(). */
        update(): this;
        pressed(action: string): boolean;
        justPressed(action: string): boolean;
        justReleased(action: string): boolean;
        /** Consecutive updates held (0 when released). */
        heldFrames(action: string): number;
        /** 0/1 for buttons, -1/0/1 for axes, 0..1 for pressure, the processed magnitude for sticks. */
        value(action: string): number;
        /** A stick's processed vector (y negative upwards, like Gamepad); the same object every call. */
        axis(action: string): { readonly x: number; readonly y: number };
        x(action: string): number;
        y(action: string): number;
        has(action: string): boolean;
        actions(): string[];
        /** Replaces one binding: a binding object or button masks. */
        rebind(action: string, binding: Binding): this;
        rebind(action: string, ...masks: number[]): this;
        /** Plain copy of every binding, for saving. */
        bindings(): Record<string, Binding>;
        /** Replaces every binding (validated first). */
        load(bindings: Record<string, Binding>): this;
    }
}
