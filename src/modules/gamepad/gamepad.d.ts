/**
 * Controller input for up to eight players, as a singleton.
 *
 * Supported controllers: DualShock 2 and other PS2 pads on both controller
 * ports, up to four per port through a multitap, and DualShock 3/4 over USB
 * (two) or Bluetooth (two, with a USB Bluetooth adapter).
 *
 * Only the two controller ports work out of the box. Multitap, USB and
 * Bluetooth each need an IOP driver that costs IOP memory, so they start
 * disabled; turn on the ones the program uses, preferably before the first
 * `update()`:
 * ```js
 * Gamepad.configure({ multitap: true, usb: true });
 * ```
 *
 * Players are logical: a controller that connects takes the lowest free
 * player and keeps it until it disconnects, whatever port or cable it uses.
 * Controllers already plugged in at start-up are assigned about half a
 * second after the first `update()`, in this order: port 1 slots A-D, port 2
 * slots A-D, USB, Bluetooth. Without multitaps, the pads on port 1 and port 2
 * therefore become players 0 and 1.
 *
 * Call `Gamepad.update()` once per frame. It polls every controller and
 * freezes a snapshot, so everything read from a `Player` during the frame is
 * cheap and consistent. `Gamepad.player(i)` always returns the same object.
 *
 * Analog values are normalized: sticks in [-1, 1], pressure and rumble
 * strength in [0, 1].
 *
 * Example:
 * ```js
 * const p1 = Gamepad.player(0);
 *
 * while (true) {
 *     Gamepad.update();
 *     if (p1.justDisconnected) pause();
 *     if (p1.justPressed(Gamepad.CROSS)) jump();
 *     const move = p1.leftStick();
 *     x += move.x * speed;
 *     if (hit) p1.rumble(0.8, 0, 200);
 *     Screen.flip();
 * }
 * ```
 */
declare namespace Gamepad {
    /** How the controller bound to a player is connected. */
    type Connection = "port" | "usb" | "bluetooth";

    /** State of one optional driver, see `Gamepad.drivers()`. */
    interface DriverState {
        /** Requested with `Gamepad.configure()`; all drivers start disabled. */
        readonly enabled: boolean;
        /** Loaded on the IOP and answering. */
        readonly ready: boolean;
    }

    /** One player. Obtain it with `Gamepad.player()`; it cannot be constructed. */
    interface Player {
        /** Player index, 0 to `MAX_PLAYERS - 1`. */
        readonly index: number;
        /** True while a controller is bound to this player. */
        readonly connected: boolean;
        /** True only on the update where a controller was bound to this player. */
        readonly justConnected: boolean;
        /** True only on the update where the controller went away. */
        readonly justDisconnected: boolean;
        /** How the controller is connected, or null when there is none. */
        readonly connection: Connection | null;
        /** Controller port (0 or 1) for `"port"` connections, otherwise -1. */
        readonly port: number;
        /** Multitap slot (0-3, 0 without a multitap) for `"port"` connections, otherwise -1. */
        readonly slot: number;
        /**
         * Kind of device, a `TYPE_*` value (`TYPE_NONE` when empty). A
         * DualShock 2 stays `TYPE_DUALSHOCK` in digital mode; see `analog`.
         */
        readonly type: DeviceType;
        /** True while the controller is in analog mode, i.e. its sticks are live. */
        readonly analog: boolean;
        /** Bitmask of the buttons held at the last update. */
        readonly buttons: number;
        /** Bitmask of the buttons held at the update before the last one. */
        readonly previousButtons: number;
        /** True when face, shoulder and d-pad buttons report real pressure (DualShock 2/3). */
        readonly hasPressure: boolean;
        /** True once the vibration motors are available. */
        readonly hasRumble: boolean;
        /**
         * Radial dead zone used by `leftStick()` and `rightStick()`, in
         * [0, 0.95]. Defaults to 0.15; set 0 for unfiltered values. Belongs to
         * the player, so it applies to whichever controller is bound.
         */
        deadzone: number;
        /** Same as `leftStick().x`, without allocating an object. */
        readonly leftX: number;
        /** Same as `leftStick().y`, without allocating an object. */
        readonly leftY: number;
        /** Same as `rightStick().x`, without allocating an object. */
        readonly rightX: number;
        /** Same as `rightStick().y`, without allocating an object. */
        readonly rightY: number;

        /** True when every button in `buttons` (e.g. `L1 | R1`) is held. */
        pressed(buttons: number): boolean;
        /** True on the update the `buttons` combination became fully held. */
        justPressed(buttons: number): boolean;
        /** True on the update the last held button of `buttons` was released. */
        justReleased(buttons: number): boolean;
        /** True when at least one button in `buttons` is held, e.g. any d-pad direction. */
        anyPressed(buttons: number): boolean;
        /** True on the update at least one button in `buttons` became held. */
        anyJustPressed(buttons: number): boolean;
        /**
         * Auto repeat for menus: true on the update a button in `buttons`
         * becomes held, then after `delayMs` (default 400) and every
         * `intervalMs` (default 100) while it stays held. Stateless, so it
         * can be called any number of times per frame.
         */
        repeatPressed(buttons: number, delayMs?: number, intervalMs?: number): boolean;
        /**
         * D-pad as a direction: each axis is -1, 0 or 1; y is negative upwards
         * like the sticks. Opposite directions held together cancel out.
         */
        dpad(): { x: -1 | 0 | 1; y: -1 | 0 | 1 };
        /**
         * Left stick in [-1, 1] with the dead zone applied; y is negative
         * upwards. Allocates an object per call; prefer `leftX`/`leftY` in
         * per-frame code for many players.
         */
        leftStick(): { x: number; y: number };
        /** Right stick in [-1, 1] with the dead zone applied; y is negative upwards. */
        rightStick(): { x: number; y: number };
        /**
         * How hard one button is pressed, in [0, 1]. Buttons without a sensor
         * report 1 while held. On a DualShock 4 only L2 and R2 are analog.
         */
        pressure(button: Button): number;
        /**
         * Vibrates the controller. `strong` drives the big motor and `weak`
         * the small one, both in [0, 1]; the small motor of the DualShock 2
         * and 3 only switches on (any `weak` above 0) or off. With
         * `durationMs` the motors stop by themselves, otherwise they run
         * until changed. Cleared when the controller disconnects; ignored
         * while the player has no controller.
         */
        rumble(strong: number, weak?: number, durationMs?: number): void;
        /** Stops both motors. */
        stopRumble(): void;
        /**
         * Requests analog (`true`, the default) or digital mode for PS2
         * controllers. When `lock` is true (default) the ANALOG button cannot
         * change it. Kept by the player and applied to every controller bound
         * to it. DualShock 3/4 are always analog.
         */
        setAnalog(enabled: boolean, lock?: boolean): void;
        /**
         * Stores the Bluetooth adapter's address in the DualShock 3/4 plugged
         * in over USB for this player, so it connects wirelessly once
         * unplugged. Needs the `usb` and `bluetooth` drivers.
         *
         * This **replaces the pairing saved in the controller**: a DualShock 3
         * paired with a PS3 stops connecting to it. It therefore requires an
         * explicit `{ overwrite: true }`; ask the user before calling it.
         *
         * Returns false when no Bluetooth adapter is present (see
         * `drivers().bluetooth.adapter`). Throws `TypeError` without the
         * confirmation or when the controller is not on USB. Blocks for a few
         * milliseconds.
         */
        pairBluetooth(options: { overwrite: true }): boolean;
        /**
         * Plain snapshot of the player (connection, type, buttons, sticks,
         * d-pad, capabilities), so `JSON.stringify(player)` and logging show
         * its state.
         */
        toJSON(): {
            index: number; connected: boolean; connection: Connection | null;
            port: number; slot: number; type: DeviceType; analog: boolean; buttons: number;
            leftStick: { x: number; y: number }; rightStick: { x: number; y: number };
            dpad: { x: number; y: number }; hasPressure: boolean; hasRumble: boolean;
            deadzone: number;
        };
    }

    /**
     * Polls every controller and captures this frame's snapshot. Call exactly
     * once per frame. The first call loads padman and the enabled drivers.
     * Throws `InternalError` when padman cannot be started; optional drivers
     * that fail are reported by `drivers()` instead.
     */
    function update(): void;
    /** Returns the persistent object of player `index` (0 to `MAX_PLAYERS - 1`). */
    function player(index: number): Player;
    /** All players, by index. The array cannot be modified. */
    const players: readonly Player[];
    /** Players with a controller bound, by index. */
    function connectedPlayers(): Player[];
    /**
     * First player whose `justPressed(buttons)` is true, or null. Useful for
     * "press START to join" screens.
     */
    function findJustPressed(buttons: number): Player | null;

    /**
     * Enables or disables optional drivers; omitted options keep their value.
     * All start disabled. An enabled driver is loaded on the next `update()`
     * and costs IOP memory from then on. Enable drivers before the first
     * update so the controllers on them join the start-up assignment order;
     * enabled later, they get players as they are found. Disabling a loaded
     * driver releases its controllers but does not unload it.
     */
    function configure(options: { multitap?: boolean; usb?: boolean; bluetooth?: boolean }): void;
    /**
     * Enabled and ready state of each optional driver. For Bluetooth,
     * `adapter` tells whether a USB Bluetooth adapter was found (one RPC; do
     * not call every frame).
     */
    function drivers(): {
        multitap: DriverState;
        usb: DriverState;
        bluetooth: DriverState & { readonly adapter: boolean };
    };
    /** True while a multitap is plugged into controller `port` (0 or 1). */
    function hasMultitap(port: number): boolean;
    /**
     * Exchanges the controllers of players `a` and `b`, with their buttons,
     * edges and rumble; either may be empty. Dead zone and analog preference
     * stay with each player and are applied to the controller it receives.
     * Use it to let whoever presses START first become player 0:
     * ```js
     * const who = Gamepad.findJustPressed(Gamepad.START);
     * if (who) Gamepad.swapPlayers(0, who.index);
     * ```
     */
    function swapPlayers(a: number, b: number): void;

    /** Number of players, and length of `players`. */
    const MAX_PLAYERS: 8;

    /*
     * Button bits. Combine them with `|` for the methods that take a mask,
     * e.g. `player.pressed(Gamepad.L1 | Gamepad.R1)`.
     */
    const SELECT: 0x0001;
    const L3: 0x0002;
    const R3: 0x0004;
    const START: 0x0008;
    const UP: 0x0010;
    const RIGHT: 0x0020;
    const DOWN: 0x0040;
    const LEFT: 0x0080;
    const L2: 0x0100;
    const R2: 0x0200;
    const L1: 0x0400;
    const R1: 0x0800;
    const TRIANGLE: 0x1000;
    const CIRCLE: 0x2000;
    const CROSS: 0x4000;
    const SQUARE: 0x8000;

    /** A single button, as taken by `pressure()`. */
    type Button = typeof SELECT | typeof L3 | typeof R3 | typeof START |
        typeof UP | typeof RIGHT | typeof DOWN | typeof LEFT |
        typeof L2 | typeof R2 | typeof L1 | typeof R1 |
        typeof TRIANGLE | typeof CIRCLE | typeof CROSS | typeof SQUARE;

    const TYPE_NONE: 0;
    const TYPE_NEJICON: 0x2;
    const TYPE_KONAMIGUN: 0x3;
    const TYPE_DIGITAL: 0x4;
    const TYPE_ANALOG: 0x5;
    const TYPE_NAMCOGUN: 0x6;
    const TYPE_DUALSHOCK: 0x7;
    const TYPE_JOGCON: 0xE;
    const TYPE_DUALSHOCK3: 0x1003;
    const TYPE_DUALSHOCK4: 0x1004;

    /** Value of `player.type`. */
    type DeviceType = typeof TYPE_NONE | typeof TYPE_NEJICON | typeof TYPE_KONAMIGUN |
        typeof TYPE_DIGITAL | typeof TYPE_ANALOG | typeof TYPE_NAMCOGUN |
        typeof TYPE_DUALSHOCK | typeof TYPE_JOGCON | typeof TYPE_DUALSHOCK3 |
        typeof TYPE_DUALSHOCK4;
}
