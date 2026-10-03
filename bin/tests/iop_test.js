import * as IOPModule from 'IOP';

globalThis.IOP = IOPModule;

let passed = 0;
let failed = 0;

function check(condition, message) {
    if (!condition) {
        failed++;
        throw new Error(message);
    }
    passed++;
    console.log(`[PASS] ${message}`);
}

const modules = IOP.getModules();
check(Array.isArray(modules) && modules.length > 0,
    'IOP.getModules returns registered modules');

const ioman = IOP.getModule('iomanX');
check(ioman.name === 'iomanX' && typeof ioman.id === 'number',
    'IOP.getModule resolves a module by name');

const sameModule = IOP.getModule(ioman.id);
check(sameModule.name === ioman.name,
    'IOP.getModule resolves a module by numeric ID');

console.log(`Result: ${passed} passed, ${failed} failed`);
