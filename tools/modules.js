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
 * Resolve the selection plus required modules and their dependencies.
 * Returned in dependency order (a module comes after everything it depends
 * on), which is the order native modules are initialized in.
 */
function resolveDependencies(selectedIds, allModules) {
    const moduleMap = new Map(allModules.map(m => [m.id, m]));
    const ordered = [];
    const state = new Map(); // id -> "visiting" | "done"

    const visit = (id, from) => {
        if (state.get(id) === 'done') return;
        if (state.get(id) === 'visiting') {
            throw new Error(`Dependency cycle involving module '${id}'`);
        }
        const mod = moduleMap.get(id);
        if (!mod) {
            const origin = from ? ` (dependency of '${from}')` : '';
            throw new Error(`Module '${id}'${origin} is not found in src/modules/`);
        }
        state.set(id, 'visiting');
        for (const depId of [...(mod.dependencies?.modules || [])].sort()) {
            visit(depId, id);
        }
        state.set(id, 'done');
        ordered.push(mod);
    };

    const roots = new Set(selectedIds);
    for (const mod of allModules) {
        if (mod.required) roots.add(mod.id);
    }
    for (const id of [...roots].sort()) {
        visit(id, null);
    }

    return ordered;
}

/*
 * Catalog contract shared by catalog.json (GitHub Pages) and the build
 * server's GET /api/catalog. Bump CATALOG_SCHEMA_VERSION on incompatible
 * changes and keep tools/athena-api.d.ts in sync.
 */
const CATALOG_SCHEMA_VERSION = 1;

function buildCatalog(modules = discoverModules()) {
    return {
        schemaVersion: CATALOG_SCHEMA_VERSION,
        name: "AthenaEnv Module Catalog",
        version: "2.0.0",
        generatedAt: new Date().toISOString(),
        modules: modules.map(m => ({
            id: m.id,
            name: m.name,
            description: m.description || "",
            category: m.category || "General",
            version: m.version || "1.0.0",
            required: !!m.required,
            default: !!m.default,
            dependencies: {
                modules: m.dependencies?.modules || [],
                iop: m.dependencies?.iop || [],
                ee_libs: m.dependencies?.ee_libs || []
            },
            global_alias: m.quickjs?.global_alias || null,
            api: {
                // C API: native sources or public headers (include/athena/).
                native: (m.sources || []).length > 0 || fs.existsSync(path.join(m._dirPath, 'include')),
                quickjs: !!m.quickjs
            }
        }))
    };
}

/**
 * Command: catalog
 * Generates catalog.json and publishes it, the typings and the API types to public/.
 */
function commandCatalog() {
    const catalog = buildCatalog();

    const outPath = path.join(ROOT_DIR, 'catalog.json');
    fs.writeFileSync(outPath, JSON.stringify(catalog, null, 2), 'utf8');
    console.log(`[Athena] Catalog generated successfully at ${outPath} (${catalog.modules.length} modules registered).`);

    const publicDir = path.join(ROOT_DIR, 'public');
    if (fs.existsSync(publicDir)) {
        fs.writeFileSync(path.join(publicDir, 'catalog.json'), JSON.stringify(catalog, null, 2), 'utf8');
        const dtsSrc = path.join(BIN_DIR, 'athena.d.ts');
        if (fs.existsSync(dtsSrc)) {
            fs.copyFileSync(dtsSrc, path.join(publicDir, 'athena.d.ts'));
        }
        fs.copyFileSync(path.join(__dirname, 'athena-api.d.ts'), path.join(publicDir, 'athena-api.d.ts'));
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
 * Generates, for the selected modules and their dependencies:
 *   - src/generated/athena/config.h    ATHENA_MODULE_<ID> defines (usable from C apps)
 *   - src/generated/native_registry.c  native lifecycle table, no scripting runtime
 *   - src/generated/js_registry.c      QuickJS bindings table and globals bootstrap
 *   - Makefile.modules                 sources per runtime, includes, libs, embedded files
 *   - bin/athena.d.ts                  TypeScript declarations
 */
const C_IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;
// Same order as the fields of AthenaNativeModule (src/core/include/athena/module.h).
const NATIVE_HOOKS = {
    register_iop: 'void',
    init: 'int',
    shutdown: 'void',
    quiesce: 'void',
    stop_requested: 'int',
};

function cString(value) {
    return value == null ? 'NULL' : `"${value}"`;
}

function selectModules(selectedArg, allModules) {
    if (!selectedArg || selectedArg === '--all' || selectedArg === 'all') {
        return allModules.map(m => m.id);
    }
    if (selectedArg === '--defaults' || selectedArg === 'defaults') {
        return allModules.filter(m => m.default).map(m => m.id);
    }
    const cleaned = selectedArg.replace(/^--modules=|^--modules/, '').trim();
    return cleaned.split(',').map(s => s.trim()).filter(Boolean);
}

/* Files a module embeds into the binary (bin2c). embed_irx keeps the
 * <name>_irx / size_<name>_irx symbol convention used by iop_manager. */
function moduleEmbeds(m) {
    const embeds = [];
    for (const irx of m.embed_irx || []) {
        if (!C_IDENT.test(irx.name || '') || !irx.irx) {
            throw new Error(`Module ${m.id}: embed_irx entries need a C identifier "name" and an "irx" path`);
        }
        embeds.push({ symbol: `${irx.name}_irx`, file: irx.irx, build: irx.build || null });
    }
    for (const e of m.embed || []) {
        if (!C_IDENT.test(e.name || '') || !e.file) {
            throw new Error(`Module ${m.id}: embed entries need a C identifier "name" and a "file" path`);
        }
        embeds.push({ symbol: e.name, file: e.file, build: e.build || null });
    }
    return embeds;
}

function generateConfigHeader(modules) {
    const defines = modules
        .map(m => `#define ATHENA_MODULE_${m.id.toUpperCase().replace(/[^A-Z0-9]/g, '_')} 1`)
        .join('\n');
    return `/*
 * Auto-generated by Athena Module Manager. DO NOT EDIT.
 * Modules in this build: ${modules.map(m => m.id).join(', ')}
 */

#ifndef ATHENA_CONFIG_H
#define ATHENA_CONFIG_H

${defines}

#endif /* ATHENA_CONFIG_H */
`;
}

function generateNativeRegistry(modules) {
    let decls = '';
    let entries = '';

    for (const m of modules) {
        const native = m.native || {};
        const fields = [];
        for (const [hook, ret] of Object.entries(NATIVE_HOOKS)) {
            const fn = native[hook];
            if (fn == null) {
                fields.push('NULL');
                continue;
            }
            if (!C_IDENT.test(fn)) {
                throw new Error(`Module ${m.id}: native.${hook} must be a C function name`);
            }
            decls += `extern ${ret} ${fn}(void);\n`;
            fields.push(fn);
        }
        for (const hook of Object.keys(native)) {
            if (!(hook in NATIVE_HOOKS)) {
                throw new Error(`Module ${m.id}: unknown native hook "${hook}"`);
            }
        }
        entries += `    { "${m.id}", ${fields.join(', ')} },\n`;
    }

    return `/*
 * Auto-generated by Athena Module Manager. DO NOT EDIT.
 * Modules in this build, in dependency order: ${modules.map(m => m.id).join(', ')}
 */

#include <stddef.h>
#include <athena/module.h>

${decls}
const AthenaNativeModule athena_native_modules[] = {
${entries}    { NULL }
};
`;
}

function generateJsRegistry(modules) {
    let decls = '';
    let entries = '';
    let bootstrapJs = '';

    for (const m of modules) {
        const qjs = m.quickjs || {};
        if (qjs.init_func) {
            decls += `extern JSModuleDef *${qjs.init_func}(JSContext *ctx);\n`;
        }
        if (qjs.cleanup_func) {
            decls += `extern void ${qjs.cleanup_func}(JSContext *ctx);\n`;
        }

        const modName = qjs.module_name || m.name;
        entries += `    { "${m.id}", "${modName}", ${cString(qjs.global_alias)}, ${qjs.init_func || 'NULL'}, ${qjs.cleanup_func || 'NULL'} },\n`;

        if (qjs.global_alias) {
            bootstrapJs += `import * as ${qjs.global_alias} from '${modName}';\\n`;
            bootstrapJs += `globalThis.${qjs.global_alias} = ${qjs.global_export || qjs.global_alias};\\n`;
        }
    }

    return `/*
 * Auto-generated by Athena Module Manager. DO NOT EDIT.
 * Modules in this build: ${modules.map(m => m.id).join(', ')}
 */

#include <stdlib.h>
#include <ath_env.h>
#include <athena_js_module.h>

${decls}
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
}

function generateMakefileModules(modules) {
    const srcs = ['src/generated/native_registry.c'];
    const jsSrcs = ['src/generated/js_registry.c'];
    const incs = new Set(['-Isrc/generated']);
    const libs = new Set();
    // symbol -> { file, build }
    const embeds = new Map();
    let exportSymbols = false;

    for (const m of modules) {
        const base = `src/modules/${m._dirName}`;
        // Public API: <athena/<id>.h>. Private headers are included with
        // relative quotes, so only selected modules are reachable.
        if (fs.existsSync(path.join(m._dirPath, 'include'))) {
            incs.add(`-I${base}/include`);
        }
        for (const include of m.includes || []) {
            if (!include.startsWith('$') && !include.startsWith('/')) {
                throw new Error(`Module ${m.id}: "includes" is only for external paths (got "${include}"). Use include/athena/ for public headers and relative includes for private ones.`);
            }
            incs.add(`-I${include}`);
        }
        for (const src of m.sources || []) {
            srcs.push(`${base}/${src}`);
        }
        for (const src of m.quickjs?.sources || []) {
            jsSrcs.push(`${base}/${src}`);
        }
        for (const lib of m.dependencies?.ee_libs || []) {
            libs.add(lib.startsWith('-') ? lib : `-l${lib}`);
        }
        for (const e of moduleEmbeds(m)) {
            const previous = embeds.get(e.symbol);
            if (previous && previous.file !== e.file) {
                throw new Error(`Module ${m.id}: "${e.symbol}" is already embedded from ${previous.file}`);
            }
            embeds.set(e.symbol, e);
        }
        if (m.build?.export_symbols) exportSymbols = true;
    }

    let embedVars = '';
    for (const [symbol, e] of embeds) {
        embedVars += `MODULE_EMBED_PATH_${symbol} = ${e.file}\n`;
        if (e.build) embedVars += `MODULE_EMBED_BUILD_${symbol} = ${e.build}\n`;
    }

    const list = items => items.length ? ` \\\n\t${items.join(' \\\n\t')}` : '';

    return `# Auto-generated by Athena Module Manager. DO NOT EDIT.
# Active modules: ${modules.map(m => m.id).join(' ')}

MODULE_IDS = ${modules.map(m => m.id).join(' ')}

# Native sources: always built, independent of the scripting runtime.
MODULE_SRCS =${list(srcs)}

# QuickJS bindings: built only with RUNTIME=quickjs.
MODULE_JS_SRCS =${list(jsSrcs)}

MODULE_INCS =${list([...incs])}

MODULE_LIBS =${libs.size ? ` ${[...libs].join(' ')}` : ''}

# Files embedded with bin2c as <symbol> / size_<symbol>. Rules live in Makefile.embed.
MODULE_EMBED =${embeds.size ? ` ${[...embeds.keys()].join(' ')}` : ''}
${embedVars}
# 1 when a module needs the binary's symbol table at runtime (erl).
MODULE_EXPORT_SYMBOLS = ${exportSymbols ? 1 : 0}
`;
}

function commandConfigure(selectedArg) {
    const allModules = discoverModules();
    const configuredModules = resolveDependencies(selectModules(selectedArg, allModules), allModules);
    console.log(`[Athena] Configuring ${configuredModules.length} module(s): ${configuredModules.map(m => m.id).join(', ')}`);

    fs.mkdirSync(path.join(GENERATED_DIR, 'athena'), { recursive: true });

    const outputs = {
        config: path.join(GENERATED_DIR, 'athena', 'config.h'),
        native: path.join(GENERATED_DIR, 'native_registry.c'),
        js: path.join(GENERATED_DIR, 'js_registry.c'),
        makefile: path.join(ROOT_DIR, 'Makefile.modules'),
        dts: path.join(BIN_DIR, 'athena.d.ts'),
    };

    fs.writeFileSync(outputs.config, generateConfigHeader(configuredModules), 'utf8');
    fs.writeFileSync(outputs.native, generateNativeRegistry(configuredModules), 'utf8');
    fs.writeFileSync(outputs.js, generateJsRegistry(configuredModules), 'utf8');
    fs.writeFileSync(outputs.makefile, generateMakefileModules(configuredModules), 'utf8');

    /* TypeScript declarations */
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

    fs.writeFileSync(outputs.dts, combinedDts.trimEnd() + '\n', 'utf8');

    // Also update catalog.json
    commandCatalog();

    console.log(`[Athena] Configuration complete!`);
    for (const [name, file] of Object.entries(outputs)) {
        console.log(`  -> ${name.padEnd(8)} ${path.relative(ROOT_DIR, file)}`);
    }
}

function parseConfigureArgs(argv) {
    let modulesList = null;
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        if (arg === '--all' || arg === '--defaults') {
            return arg;
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

export { discoverModules, resolveDependencies, buildCatalog, CATALOG_SCHEMA_VERSION };

// CLI Dispatcher (only when run directly, not when imported by the build server)
if (process.argv[1] && path.resolve(process.argv[1]) === __filename) {
    const args = process.argv.slice(2);
    const cmd = args[0] || 'catalog';

    if (cmd === 'catalog') {
        commandCatalog();
    } else if (cmd === 'list') {
        commandList();
    } else if (cmd === 'configure' || cmd.startsWith('--modules') || cmd === '--all' || cmd === '--defaults') {
        const parsed = parseConfigureArgs(args);
        commandConfigure(parsed);
    } else {
        console.log(`Usage: node tools/modules.js [catalog | list | configure [--modules mod1,mod2 | --defaults | --all]]`);
    }
}
