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

/**
 * QuickJS's `std` module (global). Only AthenaEnv's additions are declared
 * here; see the QuickJS documentation for the rest.
 */
declare namespace std {
    interface ReloadOptions {
        /**
         * Script to come back to when the new one ends, throws, or the player
         * holds SELECT+START on the pad in port 1 for a second. Used by
         * launchers such as `bin/tests/index.js`.
         */
        returnTo?: string;
    }

    interface LastRun {
        /** Path of the previous script, as it was started. */
        script: string;
        /**
         * `finished`: ended normally; `error`: threw or could not be loaded;
         * `exited`: left with SELECT+START; `reloaded`: called `std.reload()`.
         */
        status: "finished" | "error" | "exited" | "reloaded";
        /** Message and stack trace when `status` is `error`. */
        error?: string;
        /**
         * What the script printed with `console.log`/`print`, plus errors the
         * runtime reported (such as unhandled promise rejections): its last
         * 16 KiB, whole lines, starting with `[earlier output dropped]` when cut.
         */
        output: string;
    }

    /**
     * Ends the running script and starts `script` in a new JavaScript VM,
     * without restarting the ELF. Nothing after the call runs, and it cannot
     * be caught. Throws (catchably) when `script` cannot be opened.
     */
    function reload(script: string, options?: ReloadOptions): never;
    /** How the previous script ended, or null for the first script. */
    function lastRun(): LastRun | null;

    /** Runs the garbage collector. */
    function gc(): void;
    /** Contents of a UTF-8 text file, or null when it cannot be read. */
    function loadFile(path: string): string | null;
    /** Evaluates a script file in the global scope. */
    function loadScript(path: string): any;
}
