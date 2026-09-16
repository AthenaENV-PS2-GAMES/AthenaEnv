declare namespace IOP {
    interface Module {
        id: number;
        name: string;
        started: boolean;
        startAtBoot: boolean;
    }

    function getModules(): Module[];
    function getModule(nameOrId: string | number): Module;
    function loadModule(nameOrId: string | number): number;
    function reset(): void;
    function getMemoryStats(): { free: number; used: number };
}
