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
