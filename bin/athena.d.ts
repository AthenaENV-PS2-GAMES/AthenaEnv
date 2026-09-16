/**
 * AthenaEnv global JavaScript helpers.
 *
 * These declarations describe the globals available to every AthenaEnv
 * script. They are provided for editor completion and do not require imports.
 *
 * Example:
 * ```js
 * console.log('Loading game');
 * const handle = setTimeout(() => console.log('Ready'), 1000);
 * clearTimeout(handle);
 * ```
 */

declare namespace console {
    /** Writes an informational message to the EE console. */
    function log(...args: any[]): void;
    /** Writes a warning message to the EE console. */
    function warn(...args: any[]): void;
    /** Writes an error message to the EE console. */
    function error(...args: any[]): void;
}

/** Schedules a one-shot callback after `timeout` milliseconds. */
declare function setTimeout(handler: (...args: any[]) => void, timeout?: number, ...args: any[]): any;
/** Schedules a callback repeatedly using the event-loop timer queue. */
declare function setInterval(handler: (...args: any[]) => void, timeout?: number, ...args: any[]): any;
/** Cancels a timeout created by setTimeout. */
declare function clearTimeout(handle?: any): void;
/** Cancels an interval created by setInterval. */
declare function clearInterval(handle?: any): void;
/** Schedules a callback as soon as the event loop becomes idle. */
declare function setImmediate(handler: (...args: any[]) => void, ...args: any[]): any;
/** Cancels a callback created by setImmediate. */
declare function clearImmediate(handle?: any): void;


/* === Module: IOP (iop) === */
/**
 * IOP module and module-manager bindings.
 *
 * IOP module identifiers are numeric registry IDs. A module can be queried
 * by either its name or ID. Loading a module may start its dependencies.
 *
 * Example:
 * ```js
 * const fileXio = IOP.getModule('fileXio');
 * if (fileXio && !fileXio.started) IOP.loadModule(fileXio.id);
 * console.log(IOP.getMemoryStats().free);
 * ```
 */
declare namespace IOP {
    /** Metadata for one module registered with the IOP manager. */
    interface Module {
        /** Numeric registry identifier. */
        id: number;
        /** Registered module name. */
        name: string;
        /** True after the module has initialized successfully. */
        started: boolean;
        /** True when the module is part of the boot-time set. */
        startAtBoot: boolean;
    }

    /** Snapshot of IOP memory usage in bytes. */
    interface MemoryStats {
        /** Available IOP RAM. */
        free: number;
        /** RAM currently in use by IOP modules. */
        used: number;
    }

    /** Returns a snapshot of all registered IOP modules. */
    function getModules(): Module[];
    /** Resolves a module by its name or numeric registry ID. */
    function getModule(nameOrId: string | number): Module;
    /** Loads and initializes a module and its dependencies. */
    function loadModule(nameOrId: string | number): number;
    /** Resets the IOP and reinstalls the registered boot modules. */
    function reset(): void;
    /** Returns free and used IOP RAM in bytes. */
    function getMemoryStats(): MemoryStats;
}


/* === Module: Matrix4 (matrix4) === */
/**
 * Four-by-four transformation matrix using the PS2/AthenaEnv layout.
 *
 * Values are stored in column-major order. Translation components are at
 * indices 12, 13 and 14; index 15 is the homogeneous component.
 *
 * Example:
 * ```js
 * const transform = new Matrix4();
 * transform.set(12, 10).set(13, 20).set(14, 30);
 * const inverse = transform.clone().invert();
 * console.log(inverse.get(12), inverse.get(13), inverse.get(14));
 * ```
 */
declare class Matrix4 {
    /** Creates identity matrix, or initializes all 16 values when supplied. */
    constructor();
    constructor(
        m00: number, m01: number, m02: number, m03: number,
        m10: number, m11: number, m12: number, m13: number,
        m20: number, m21: number, m22: number, m23: number,
        m30: number, m31: number, m32: number, m33: number
    );
    /** Number of scalar components in the matrix. */
    readonly length: 16;
    /** Reads a scalar component at index 0..15. */
    get(index: number): number;
    /** Writes a scalar component at index 0..15 and returns this matrix. */
    set(index: number, value: number): this;
    /** Compares all 16 components exactly. */
    equals(value: Matrix4): boolean;
    /** Compares all components using an absolute epsilon tolerance. */
    equalsEpsilon(value: Matrix4, epsilon: number): boolean;
    /** Returns the 16 components as a new array. */
    toArray(): number[];
    /** Copies 16 values from an array-like object into this matrix. */
    fromArray(values: ArrayLike<number>): this;
    /** Returns an independent copy of this matrix. */
    clone(): Matrix4;
    /** Copies another matrix into this matrix. */
    copy(value: Matrix4): this;
    /** Returns the product of this matrix and `value`. */
    multiply(value: Matrix4): Matrix4;
    /** Replaces this matrix with identity. */
    identity(): this;
    /** Transposes this matrix in place. */
    transpose(): this;
    /** Inverts this matrix in place; throws for a singular matrix. */
    invert(): this;
    /** Returns a readable 16-value representation. */
    toString(): string;
}


/* === Module: Mutex (mutex) === */
/**
 * Native EE mutex primitives.
 *
 * Mutexes protect native/application data shared by callbacks or threads.
 * They do not make arbitrary QuickJS runtime access thread-safe; JavaScript
 * execution must still follow AthenaEnv's runtime-gate rules.
 *
 * Example:
 * ```js
 * const lock = Mutex.new();
 * Mutex.lock(lock);
 * try {
 *     // Update shared native/application state.
 * } finally {
 *     Mutex.unlock(lock);
 *     Mutex.destroy(lock);
 * }
 * ```
 */
declare namespace Mutex {
    /** Opaque handle returned by `Mutex.new()`. */
    interface Handle {
        readonly __brand: 'Mutex';
    }

    /** Creates an unlocked native mutex. */
    function new(): Handle;

    /** Blocks until acquired and returns the native EE result code. */
    function lock(mutex: Handle): number;

    /** Releases the mutex and returns the native EE result code. */
    function unlock(mutex: Handle): number;

    /** Releases the native mutex. Do not use `mutex` afterwards. */
    function destroy(mutex: Handle): void;
}


/* === Module: System Core (system) === */
/**
 * PS2 system, filesystem, timing and hardware helpers.
 *
 * Paths use the PS2 device syntax such as `host:/`, `mass:/` or `mc0:/`.
 * Return values from filesystem and device operations are native result codes;
 * callers should check them before continuing.
 *
 * Example:
 * ```js
 * console.log(System.bootPath);
 * for (const entry of System.listDir('host:/')) {
 *     console.log(entry.dir ? '[DIR]' : entry.size, entry.name);
 * }
 * System.sleep(16);
 * ```
 */
declare namespace System {
    /** One directory entry returned by `listDir()`. */
    interface DirectoryEntry {
        /** File or directory name. */
        name: string;
        /** File size in bytes; directory sizes may be zero. */
        size: number;
        /** True when this entry is a directory. */
        dir: boolean;
    }

    /** Memory counters returned by `getMemoryStats()`. */
    interface MemoryStats {
        /** Core/binary footprint in bytes. */
        core: number;
        /** Reserved native stack in bytes. */
        nativeStack: number;
        /** Current native allocations in bytes. */
        allocs: number;
        /** Total reported usage in bytes. */
        used: number;
    }

    /** EE CPU information returned by `getCPUInfo()`. */
    interface CPUInfo {
        /** EE CPU implementation identifier. */
        implementation: number;
        /** EE CPU revision identifier. */
        revision: number;
        /** Installed EE RAM size in bytes. */
        RAMSize: number;
        /** EE bus clock frequency. */
        BUSClock: number;
        /** EE CPU clock frequency. */
        CPUClock: number;
        /** PS2 machine type identifier. */
        MachineType: number;
    }

    /** Memory-card status returned by `getMCInfo()`. */
    interface MemoryCardInfo {
        /** Memory-card type identifier. */
        type: number;
        /** Free memory reported by the card driver. */
        freemem: number;
        /** Format/status flag reported by the card driver. */
        format: number;
    }

    /** GS GPU information returned by `getGPUInfo()`. */
    interface GPUInfo {
        revision: number;
        id: number;
    }

    /** One registered filesystem/device entry. */
    interface DeviceInfo {
        name: string;
        desc: string;
    }

    /** The path from which the application booted (e.g. "mass0:/", "cdfs:/") */
    const bootPath: string;
    /** Legacy alias for bootPath. */
    const boot_path: string;

    /** Lists entries in a directory or path relative to `bootPath`. */
    function listDir(path?: string): DirectoryEntry[];

    /** Removes an empty directory and returns the underlying system result. */
    function removeDirectory(path: string): number;

    /** Copies a file and returns zero on success. */
    function copyFile(source: string, destination: string): number;

    /** Moves or renames a file and returns zero on success. */
    function moveFile(source: string, destination: string): number;
    /** Renames a file or directory and returns the native result code. */
    function rename(source: string, destination: string): number;

    /** Returns raw EE CPU clock ticks. */
    function getTicks(): number;

    /** Returns high-resolution elapsed time in milliseconds. */
    function getMilliseconds(): number;

    /** Suspends the current EE thread for the specified milliseconds. */
    function sleep(ms: number): void;

    /** Returns currently used EE RAM in bytes. */
    function getUsedMemory(): number;

    /** Returns remaining available EE RAM in bytes. */
    function getFreeMemory(): number;

    /** Yields briefly to the EE scheduler. */
    function delay(): void;

    /** Returns memory counters from the legacy System API. */
    function getMemoryStats(): MemoryStats;

    /** Returns basic EE CPU and memory information. */
    function getCPUInfo(): CPUInfo;

    /** Returns basic GS GPU information. */
    function getGPUInfo(): GPUInfo;

    /** Returns the console temperature in Celsius when supported. */
    function getTemperature(): number | undefined;

    /** Returns memory-card information for a controller port (0 or 1). */
    function getMCInfo(port?: number): MemoryCardInfo;

    /** Returns information about a mass-storage block device. */
    function getBDMInfo(device: string): { name: string; index: number } | undefined;

    /** Returns currently registered file-system devices. */
    function devices(): DeviceInfo[];

    /** Mounts a block device at a file-system mount point. */
    function mount(mountpoint: string, blockdev: string, mode?: number): number;

    /** Unmounts a file-system device. */
    function umount(device: string): number;

    /** Loads an ELF using the legacy Athena loader. */
    function loadELF(path: string, args?: string[]): number;

    /** Enables or disables the legacy dark-mode flag. */
    function setDarkMode(enabled: boolean): void;

    /** Forces a QuickJS garbage-collection cycle. */
    function gc(): void;

    /** Exit application to the PS2 browser/OSDSYS */
    function exit(): void;

    /** Alias for exiting to the PS2 browser/OSDSYS. */
    function exitToBrowser(): void;
}


/* === Module: Thread (thread) === */
/**
 * EE thread management.
 *
 * Thread callbacks execute on native EE worker threads. Keep callbacks short,
 * avoid direct QuickJS runtime access from native workers, and use the
 * documented AthenaEnv synchronization boundaries.
 *
 * Example:
 * ```js
 * const worker = Thread.new(() => {
 *     System.delay();
 * }, 'worker', 16384, 16);
 * Thread.start(worker);
 * console.log(Thread.getStatus(worker));
 * Thread.destroy(worker);
 * ```
 */
declare namespace Thread {
    /** Opaque handle returned by `Thread.new()`. */
    interface Handle {
        readonly __brand: 'Thread';
    }

    /** Snapshot of one tracked native thread. */
    interface TaskInfo {
        /** Native EE thread ID. */
        id: number;
        /** Thread name. */
        name: string;
        /** Native status code. */
        status: number;
        /** Configured stack size in bytes. */
        stack: number;
    }

    /**
     * Creates a native EE thread.
     * @param callback Function executed by the new thread.
     * @param name Optional name, limited to 63 characters.
     * @param stackSize Stack size in bytes; defaults to 16384.
     * @param priority EE priority from 1 to 127; defaults to 16.
     */
    function new(
        callback: () => void,
        name?: string,
        stackSize?: number,
        priority?: number
    ): Handle;

    /** Starts execution and returns the native EE result code. */
    function start(thread: Handle): number;

    /** Requests thread termination and returns the native EE result code. */
    function stop(thread: Handle): number;

    /** Returns the native EE thread ID. */
    function getId(thread: Handle): number;

    /** Returns the current thread name. */
    function getName(thread: Handle): string;

    /** Replaces the thread name. */
    function setName(thread: Handle, name: string): void;

    /** Returns the native EE status code. */
    function getStatus(thread: Handle): number;

    /** Releases the thread handle. Stop it first; do not reuse the object. */
    function destroy(thread: Handle): void;

    /** Returns all active/tracked threads. */
    function list(): TaskInfo[];

    /** Force-terminates a native thread by ID. */
    function kill(id: number): number;
}


/* === Module: Timer (timer) === */
/**
 * Manual native timer objects.
 *
 * Timer values are represented in the module's native clock-tick units.
 * These timers are distinct from the global event-loop functions such as
 * `setTimeout`.
 *
 * Example:
 * ```js
 * const timer = Timer.new();
 * Timer.pause(timer);
 * Timer.setTime(timer, 0);
 * Timer.resume(timer);
 * console.log(Timer.isPlaying(timer));
 * Timer.destroy(timer);
 * ```
 */
declare namespace Timer {
    /** Opaque handle returned by `Timer.new()`. */
    interface Handle {
        readonly __brand: 'Timer';
    }

    /** Creates a running timer. */
    function new(): Handle;

    /** Returns elapsed native clock ticks, frozen while paused. */
    function getTime(timer: Handle): number;

    /** Replaces the elapsed time in native clock ticks. */
    function setTime(timer: Handle, value: number): void;

    /** Pauses without resetting the elapsed time. */
    function pause(timer: Handle): void;

    /** Resumes a paused timer. */
    function resume(timer: Handle): void;

    /** Sets elapsed time to zero while preserving the timer object. */
    function reset(timer: Handle): void;

    /** Returns true when the timer is actively advancing. */
    function isPlaying(timer: Handle): boolean;

    /** Releases the native timer. Do not use `timer` afterwards. */
    function destroy(timer: Handle): void;
}


/* === Module: Vector (vector) === */
/**
 * PS2-aligned vector types.
 *
 * `Vector2`, `Vector3` and `Vector4` are separate JavaScript classes exposed
 * by the `Vector` module. Arithmetic methods return new vectors and do not
 * mutate their operands. `div()` rejects zero components.
 *
 * Example:
 * ```js
 * import * as Vector from 'Vector';
 * const direction = new Vector.Vector3(3, 4, 0);
 * console.log(direction.norm());
 * const right = direction.cross(new Vector.Vector3(0, 0, 1));
 * ```
 */
declare class Vector2 {
    /** Creates a two-component vector. */
    constructor(x: number, y: number);
    /** Horizontal component. */
    x: number;
    /** Vertical component. */
    y: number;
    /** Returns Euclidean length. */
    norm(): number;
    /** Returns the dot product. */
    dot(value: Vector2): number;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector2): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector2): number;
    /** Returns the component-wise sum. */
    add(value: Vector2): Vector2;
    /** Returns the component-wise difference. */
    sub(value: Vector2): Vector2;
    /** Returns the component-wise product. */
    mul(value: Vector2): Vector2;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector2): Vector2;
    /** Returns a readable component representation. */
    toString(): string;
}

declare class Vector3 {
    /** Creates a three-component vector. */
    constructor(x: number, y: number, z: number);
    /** X component. */
    x: number;
    /** Y component. */
    y: number;
    /** Z component. */
    z: number;
    /** Returns Euclidean length. */
    norm(): number;
    /** Returns the dot product. */
    dot(value: Vector3): number;
    /** Returns the 3D cross product. */
    cross(value: Vector3): Vector3;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector3): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector3): number;
    /** Returns the component-wise sum. */
    add(value: Vector3): Vector3;
    /** Returns the component-wise difference. */
    sub(value: Vector3): Vector3;
    /** Returns the component-wise product. */
    mul(value: Vector3): Vector3;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector3): Vector3;
    /** Returns a readable component representation. */
    toString(): string;
}

declare class Vector4 {
    /** Creates a homogeneous four-component vector. */
    constructor(x: number, y: number, z: number, w: number);
    /** X component. */
    x: number;
    /** Y component. */
    y: number;
    /** Z component. */
    z: number;
    /** Homogeneous component: commonly 1 for points and 0 for directions. */
    w: number;
    /** Returns four-dimensional Euclidean length. */
    norm(): number;
    /** Returns the four-component dot product. */
    dot(value: Vector4): number;
    /** Returns the cross product with homogeneous component cleared. */
    cross(value: Vector4): Vector4;
    /** Returns Euclidean distance to another vector. */
    distance(value: Vector4): number;
    /** Returns squared distance without taking a square root. */
    distance2(value: Vector4): number;
    /** Returns the component-wise sum. */
    add(value: Vector4): Vector4;
    /** Returns the component-wise difference. */
    sub(value: Vector4): Vector4;
    /** Returns the component-wise product. */
    mul(value: Vector4): Vector4;
    /** Returns the component-wise quotient; zero divisors throw. */
    div(value: Vector4): Vector4;
    /** Returns a readable component representation. */
    toString(): string;
}
