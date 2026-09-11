/**
 * AthenaEnv Type Definitions for VS Code IntelliSense (Pure JavaScript)
 * Generated based on selected modules.
 */

declare namespace console {
    function log(...args: any[]): void;
    function warn(...args: any[]): void;
    function error(...args: any[]): void;
}

declare function setTimeout(handler: (...args: any[]) => void, timeout?: number, ...args: any[]): any;
declare function setInterval(handler: (...args: any[]) => void, timeout?: number, ...args: any[]): any;
declare function clearTimeout(handle?: any): void;
declare function clearInterval(handle?: any): void;
declare function setImmediate(handler: (...args: any[]) => void, ...args: any[]): any;
declare function clearImmediate(handle?: any): void;


/* === Module: System Core (system) === */
declare namespace System {
    /** The path from which the application booted (e.g. "mass0:/", "cdfs:/") */
    const bootPath: string;
    /** Legacy alias for bootPath. */
    const boot_path: string;

    /** Lists entries in the current directory or in a path relative to bootPath. */
    function listDir(path?: string): { name: string; size: number; dir: boolean }[];

    /** Removes an empty directory and returns the underlying system result. */
    function removeDirectory(path: string): number;

    /** Copies a file and returns zero on success. */
    function copyFile(source: string, destination: string): number;

    /** Moves or renames a file and returns zero on success. */
    function moveFile(source: string, destination: string): number;
    function rename(source: string, destination: string): number;

    /** Returns raw CPU clock ticks */
    function getTicks(): number;

    /** Returns high-resolution elapsed time in milliseconds */
    function getMilliseconds(): number;

    /** Sleep the EE thread for the specified number of milliseconds */
    function sleep(ms: number): void;

    /** Returns currently used RAM in bytes */
    function getUsedMemory(): number;

    /** Returns remaining available RAM in bytes */
    function getFreeMemory(): number;

    /** Yields briefly to the EE scheduler. */
    function delay(): void;

    /** Returns memory counters from the legacy System API. */
    function getMemoryStats(): {
        core: number;
        nativeStack: number;
        allocs: number;
        used: number;
    };

    /** Returns basic EE CPU and memory information. */
    function getCPUInfo(): {
        implementation: number;
        revision: number;
        RAMSize: number;
        BUSClock: number;
        CPUClock: number;
        MachineType: number;
    };

    /** Returns basic GS GPU information. */
    function getGPUInfo(): { revision: number; id: number };

    /** Returns the console temperature in Celsius when supported. */
    function getTemperature(): number | undefined;

    /** Returns memory-card information for a controller port (0 or 1). */
    function getMCInfo(port?: number): {
        type: number;
        freemem: number;
        format: number;
    };

    /** Returns information about a mass-storage block device. */
    function getBDMInfo(device: string): { name: string; index: number } | undefined;

    /** Returns currently registered file-system devices. */
    function devices(): { name: string; desc: string }[];

    /** Mounts a block device at a file-system mount point. */
    function mount(mountpoint: string, blockdev: string, mode?: number): number;

    /** Unmounts a file-system device. */
    function umount(device: string): number;

    /** Loads an ELF using the legacy Athena loader. */
    function loadELF(path: string, args?: string[]): number;

    /** Enables or disables the legacy dark-mode flag. */
    function setDarkMode(enabled: boolean): void;

    /** Force QuickJS garbage collection */
    function gc(): void;

    /** Exit application to the PS2 browser/OSDSYS */
    function exit(): void;

    /** Alias for exiting to the PS2 browser/OSDSYS. */
    function exitToBrowser(): void;
}


/* === Module: Timer (timer) === */
declare namespace Timer {
    /** Creates a running timer object. */
    function new(): object;

    /** Returns elapsed clock ticks, or the frozen value while paused. */
    function getTime(timer: object): number;

    /** Sets the elapsed time in clock ticks. */
    function setTime(timer: object, value: number): void;

    /** Pauses the timer without resetting its elapsed time. */
    function pause(timer: object): void;

    /** Resumes a paused timer. */
    function resume(timer: object): void;

    /** Resets elapsed time to zero. */
    function reset(timer: object): void;

    /** Returns whether the timer is currently running. */
    function isPlaying(timer: object): boolean;

    /** Releases the native timer immediately; the object must not be reused. */
    function destroy(timer: object): void;
}
