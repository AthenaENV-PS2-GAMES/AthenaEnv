#!/usr/bin/env node
/**
 * AthenaEnv Module Manager
 * Handles module discovery, catalog generation, and build configuration.
 * Compatible with Bun and Node.js (zero dependencies).
 */

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT_DIR = path.resolve(__dirname, '..');
const MODULES_DIR = path.join(ROOT_DIR, 'src', 'modules');
const GENERATED_DIR = path.join(ROOT_DIR, 'src', 'generated');
const BIN_DIR = path.join(ROOT_DIR, 'bin');

/**
 * Scan and load all modules from src/modules/
 */
function discoverModules() {
    if (!fs.existsSync(MODULES_DIR)) {
        return [];
    }

    const entries = fs.readdirSync(MODULES_DIR, { withFileTypes: true });
    const modules = [];

    for (const entry of entries) {
        if (!entry.isDirectory()) continue;
        const manifestPath = path.join(MODULES_DIR, entry.name, 'module.json');
        if (fs.existsSync(manifestPath)) {
            try {
                const content = fs.readFileSync(manifestPath, 'utf8');
                const manifest = JSON.parse(content);
                manifest._dirName = entry.name;
                manifest._dirPath = path.join(MODULES_DIR, entry.name);
                modules.push(manifest);
            } catch (err) {
                console.error(`[Error] Failed to parse manifest at ${manifestPath}:`, err.message);
            }
        }
    }

    return modules;
}

/**
 * Resolve dependency order and ensure all required dependencies are present.
 */
function resolveDependencies(selectedIds, allModules) {
    const moduleMap = new Map(allModules.map(m => [m.id, m]));
    const resolved = new Set();
    const toProcess = [...selectedIds];

    // Always include required modules
    for (const mod of allModules) {
        if (mod.required && !toProcess.includes(mod.id)) {
            toProcess.push(mod.id);
        }
    }

    while (toProcess.length > 0) {
        const id = toProcess.shift();
        if (resolved.has(id)) continue;

        const mod = moduleMap.get(id);
        if (!mod) {
            throw new Error(`Module '${id}' was requested or required as a dependency, but is not found in src/modules/`);
        }

        resolved.add(id);

        if (mod.dependencies && Array.isArray(mod.dependencies.modules)) {
            for (const depId of mod.dependencies.modules) {
                if (!resolved.has(depId)) {
                    toProcess.push(depId);
                }
            }
        }
    }

    return Array.from(resolved).map(id => moduleMap.get(id));
}

/**
 * Command: catalog
 * Generates catalog.json for the web portal
 */
function commandCatalog() {
    const modules = discoverModules();
    const catalog = {
        name: "AthenaEnv Module Catalog",
        version: "2.0.0",
        generatedAt: new Date().toISOString(),
        modules: modules.map(m => ({
            id: m.id,
            name: m.name,
            description: m.description,
            category: m.category || "General",
            version: m.version || "1.0.0",
            required: !!m.required,
            default: !!m.default,
            dependencies: m.dependencies || { modules: [], iop: [], ee_libs: [] },
            global_alias: m.quickjs?.global_alias || null
        }))
    };

    const outPath = path.join(ROOT_DIR, 'catalog.json');
    fs.writeFileSync(outPath, JSON.stringify(catalog, null, 2), 'utf8');
    console.log(`[Athena] Catalog generated successfully at ${outPath} (${modules.length} modules registered).`);

    const publicDir = path.join(ROOT_DIR, 'public');
    if (fs.existsSync(publicDir)) {
        fs.writeFileSync(path.join(publicDir, 'catalog.json'), JSON.stringify(catalog, null, 2), 'utf8');
        const dtsSrc = path.join(BIN_DIR, 'athena.d.ts');
        if (fs.existsSync(dtsSrc)) {
            fs.copyFileSync(dtsSrc, path.join(publicDir, 'athena.d.ts'));
        }
    }
    return catalog;
}

/**
 * Command: list
 */
function commandList() {
    const modules = discoverModules();
    console.log('\n========================================');
    console.log('         AthenaEnv Module Catalog       ');
    console.log('========================================\n');
    for (const m of modules) {
        const reqStr = m.required ? '[REQUIRED]' : (m.default ? '[DEFAULT]' : '[OPTIONAL]');
        console.log(`- ${m.id} (${m.name}) ${reqStr}`);
        console.log(`  Category: ${m.category || 'General'}`);
        console.log(`  Description: ${m.description}`);
        if (m.dependencies?.modules?.length) {
            console.log(`  Dependencies: ${m.dependencies.modules.join(', ')}`);
        }
        if (m.dependencies?.ee_libs?.length) {
            console.log(`  EE Libs: ${m.dependencies.ee_libs.join(' ')}`);
        }
        console.log('');
    }
}

/**
 * Command: configure
 * Generates src/generated/modules_registry.c, Makefile.modules, and bin/athena.d.ts
 */
function commandConfigure(selectedArg) {
    const allModules = discoverModules();
    let selectedIds = [];

    if (!selectedArg || selectedArg === '--all' || selectedArg === 'all') {
        selectedIds = allModules.map(m => m.id);
    } else {
        const cleaned = selectedArg.replace(/^--modules=|^--modules/, '').trim();
        selectedIds = cleaned.split(',').map(s => s.trim()).filter(Boolean);
    }

    const configuredModules = resolveDependencies(selectedIds, allModules);
    console.log(`[Athena] Configuring ${configuredModules.length} module(s): ${configuredModules.map(m => m.id).join(', ')}`);

    // Ensure generated directory exists
    if (!fs.existsSync(GENERATED_DIR)) {
        fs.mkdirSync(GENERATED_DIR, { recursive: true });
    }

    /* 1. Generate src/generated/modules_registry.c */
    let externDecls = '';
    let entries = '';
    let bootstrapJs = '';

    for (const m of configuredModules) {
        const qjs = m.quickjs || {};
        if (qjs.init_func) {
            externDecls += `extern JSModuleDef *${qjs.init_func}(JSContext *ctx);\n`;
        }
        if (qjs.cleanup_func) {
            externDecls += `extern void ${qjs.cleanup_func}(JSContext *ctx);\n`;
        }

        const cleanupVal = qjs.cleanup_func || 'NULL';
        const initVal = qjs.init_func || 'NULL';
        const modName = qjs.module_name || m.name;
        const globalAlias = qjs.global_alias ? `"${qjs.global_alias}"` : 'NULL';

        entries += `    { "${m.id}", "${modName}", ${globalAlias}, ${initVal}, ${cleanupVal} },\n`;

        if (qjs.global_alias) {
            bootstrapJs += `import * as ${qjs.global_alias} from '${modName}';\\n`;
            bootstrapJs += `globalThis.${qjs.global_alias} = ${qjs.global_alias};\\n`;
        }
    }

    const registryC = `/*
 * Auto-generated by Athena Module Manager.
 * DO NOT EDIT DIRECTLY.
 * Configured modules: ${configuredModules.map(m => m.id).join(', ')}
 */

#include <stdlib.h>
#include <ath_env.h>
#include <athena_module.h>

/* Module external initializers & cleanups */
${externDecls}
static const AthenaModuleEntry athena_registered_modules[] = {
${entries}    { NULL, NULL, NULL, NULL, NULL }
};

void athena_register_all_modules(JSContext *ctx) {
    for (int i = 0; athena_registered_modules[i].id != NULL; i++) {
        if (athena_registered_modules[i].init) {
            athena_registered_modules[i].init(ctx);
        }
    }
}

void athena_cleanup_all_modules(JSContext *ctx) {
    for (int i = 0; athena_registered_modules[i].id != NULL; i++) {
        if (athena_registered_modules[i].cleanup) {
            athena_registered_modules[i].cleanup(ctx);
        }
    }
}

static const char *modules_bootstrap_code = 
    "${bootstrapJs}";

const char *athena_get_modules_bootstrap_script(void) {
    return modules_bootstrap_code;
}
`;

    const registryPath = path.join(GENERATED_DIR, 'modules_registry.c');
    fs.writeFileSync(registryPath, registryC, 'utf8');

    /* 2. Generate Makefile.modules */
    const srcs = ['src/generated/modules_registry.c'];
    const incs = ['-Isrc/generated'];
    const libs = new Set();

    for (const m of configuredModules) {
        incs.push(`-Isrc/modules/${m._dirName}`);
        if (m.includes && Array.isArray(m.includes)) {
            for (const include of m.includes) {
                if (include === '.') continue;
                if (include.startsWith('$') || include.startsWith('/')) {
                    incs.push(`-I${include}`);
                } else {
                    incs.push(`-Isrc/modules/${m._dirName}/${include}`);
                }
            }
        }
        if (m.sources && Array.isArray(m.sources)) {
            for (const src of m.sources) {
                srcs.push(`src/modules/${m._dirName}/${src}`);
            }
        }
        if (m.dependencies?.ee_libs && Array.isArray(m.dependencies.ee_libs)) {
            for (const lib of m.dependencies.ee_libs) {
                libs.add(lib.startsWith('-') ? lib : `-l${lib}`);
            }
        }
    }

    const makefileModulesContent = `# Auto-generated by Athena Module Manager. DO NOT EDIT.
# Active modules: ${configuredModules.map(m => m.id).join(' ')}

MODULE_SRCS = \\
\t${srcs.join(' \\\n\t')}

MODULE_INCS = \\
\t${incs.join(' \\\n\t')}

MODULE_LIBS =${Array.from(libs).length ? ` ${Array.from(libs).join(' ')}` : ''}
`;

    const makefileModulesPath = path.join(ROOT_DIR, 'Makefile.modules');
    fs.writeFileSync(makefileModulesPath, makefileModulesContent, 'utf8');

    /* 3. Generate bin/athena.d.ts */
    const coreDtsPath = path.join(MODULES_DIR, 'core.d.ts');
    let combinedDts = '';

    if (fs.existsSync(coreDtsPath)) {
        combinedDts += fs.readFileSync(coreDtsPath, 'utf8') + '\n\n';
    }

    for (const m of configuredModules) {
        if (m.types) {
            const typesPath = path.join(m._dirPath, m.types);
            if (fs.existsSync(typesPath)) {
                combinedDts += `/* === Module: ${m.name} (${m.id}) === */\n`;
                combinedDts += fs.readFileSync(typesPath, 'utf8') + '\n\n';
            }
        }
    }

    const dtsPath = path.join(BIN_DIR, 'athena.d.ts');
    fs.writeFileSync(dtsPath, combinedDts.trimEnd() + '\n', 'utf8');

    // Also update catalog.json
    commandCatalog();

    console.log(`[Athena] Configuration complete!`);
    console.log(`  -> Registry: ${registryPath}`);
    console.log(`  -> Makefile: ${makefileModulesPath}`);
    console.log(`  -> Types:    ${dtsPath}`);
}

function parseConfigureArgs(argv) {
    let modulesList = null;
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '--all') {
            return '--all';
        }
        if (arg.startsWith('--modules=')) {
            modulesList = arg.substring('--modules='.length);
        } else if (arg === '--modules' || arg === '-m') {
            modulesList = argv[i + 1] || '';
            i++;
        } else if (arg !== 'configure' && !arg.startsWith('-') && !modulesList) {
            modulesList = arg;
        }
    }
    return modulesList;
}

// CLI Dispatcher
const args = process.argv.slice(2);
const cmd = args[0] || 'catalog';

if (cmd === 'catalog') {
    commandCatalog();
} else if (cmd === 'list') {
    commandList();
} else if (cmd === 'configure' || cmd.startsWith('--modules') || cmd === '--all') {
    const parsed = parseConfigureArgs(args);
    commandConfigure(parsed);
} else {
    console.log(`Usage: bun tools/modules.js [catalog | list | configure [--modules mod1,mod2 | --all]]`);
}
