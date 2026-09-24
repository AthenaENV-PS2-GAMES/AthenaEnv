#!/usr/bin/env node
/**
 * AthenaEnv build server.
 *
 * Builds a custom AthenaEnv binary (or native SDK) for a module selection, as
 * requested by the module picker on the site. Zero dependencies; needs node,
 * git, make and the PS2 toolchain (see Dockerfile.server).
 *
 *   POST /api/builds            { "modules": ["gamepad", "font"], "runtime": "quickjs" | "native" }
 *   GET  /api/builds/:id        build status, resolved modules, artifacts
 *   GET  /api/builds/:id/:file  download an artifact (or build.log)
 *   GET  /api/catalog           modules this server can build
 *
 * Builds run one at a time: `configure` rewrites Makefile.modules and
 * src/generated in the working tree. Identical requests (same commit, runtime
 * and resolved modules) share one cached result.
 *
 * Environment:
 *   PORT            listen port (8080)
 *   ATHENA_ROOT     AthenaEnv checkout used for builds (repository root)
 *   CACHE_DIR       where results are kept (<ATHENA_ROOT>/.build-cache)
 *   ALLOWED_ORIGIN  CORS origin allowed to call the API (*)
 *   MAX_QUEUE       pending builds accepted before answering 503 (16)
 *   BUILD_TIMEOUT   seconds before a build is killed (900)
 */

import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { discoverModules, resolveDependencies } from '../modules.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ATHENA_ROOT = path.resolve(process.env.ATHENA_ROOT || path.join(HERE, '..', '..'));
const CACHE_DIR = path.resolve(process.env.CACHE_DIR || path.join(ATHENA_ROOT, '.build-cache'));
const PORT = Number(process.env.PORT || 8080);
const ALLOWED_ORIGIN = process.env.ALLOWED_ORIGIN || '*';
const MAX_QUEUE = Number(process.env.MAX_QUEUE || 16);
const BUILD_TIMEOUT_MS = Number(process.env.BUILD_TIMEOUT || 900) * 1000;
const MAX_BODY = 16 * 1024;

const RUNTIMES = {
    quickjs: {
        make: ['all', 'RUNTIME=quickjs'],
        artifacts: { 'athena.elf': 'bin/athena.elf', 'athena_pkd.elf': 'bin/athena_pkd.elf', 'athena.d.ts': 'bin/athena.d.ts' },
    },
    native: {
        make: ['sdk', 'RUNTIME=native'],
        artifacts: { 'athena-sdk.tar.gz': 'dist/athena-sdk.tar.gz' },
    },
};

const ID_PATTERN = /^[0-9a-f]{64}$/;

function commit() {
    try {
        return execFileSync('git', ['rev-parse', 'HEAD'], { cwd: ATHENA_ROOT }).toString().trim();
    } catch {
        return 'unknown';
    }
}

const COMMIT = commit();
const MODULES = discoverModules();
const MODULE_IDS = new Set(MODULES.map(m => m.id));

/* ------------------------------------------------------------------ */
/* Build queue                                                        */
/* ------------------------------------------------------------------ */

const jobs = new Map();   // id -> job (in memory; finished jobs are also on disk)
const queue = [];         // ids waiting to run
let running = null;

function jobDir(id) {
    return path.join(CACHE_DIR, id);
}

function readStatus(id) {
    try {
        return JSON.parse(fs.readFileSync(path.join(jobDir(id), 'status.json'), 'utf8'));
    } catch {
        return null;
    }
}

function writeStatus(job) {
    fs.mkdirSync(jobDir(job.id), { recursive: true });
    const { id, status, runtime, modules, requested, commit, created, finished, error, artifacts } = job;
    fs.writeFileSync(path.join(jobDir(id), 'status.json'),
        JSON.stringify({ id, status, runtime, modules, requested, commit, created, finished, error, artifacts }, null, 2));
}

function publicJob(job) {
    const position = job.status === 'queued' ? queue.indexOf(job.id) + 1 : 0;
    return {
        id: job.id,
        status: job.status,
        position,
        runtime: job.runtime,
        modules: job.modules,
        requested: job.requested,
        commit: job.commit,
        error: job.error || null,
        artifacts: (job.artifacts || []).map(name => ({ name, url: `/api/builds/${job.id}/${name}` })),
        log: `/api/builds/${job.id}/build.log`,
    };
}

function run(command, args, log) {
    return new Promise(resolve => {
        log.write(`$ ${command} ${args.join(' ')}\n`);
        const child = spawn(command, args, { cwd: ATHENA_ROOT, env: process.env });
        const timer = setTimeout(() => {
            log.write(`\n[build-server] timed out after ${BUILD_TIMEOUT_MS / 1000}s\n`);
            child.kill('SIGKILL');
        }, BUILD_TIMEOUT_MS);
        child.stdout.pipe(log, { end: false });
        child.stderr.pipe(log, { end: false });
        child.on('error', err => log.write(`\n[build-server] ${err.message}\n`));
        child.on('close', code => {
            clearTimeout(timer);
            resolve(code);
        });
    });
}

async function build(job) {
    const dir = jobDir(job.id);
    fs.mkdirSync(dir, { recursive: true });
    const log = fs.createWriteStream(path.join(dir, 'build.log'));
    const runtime = RUNTIMES[job.runtime];

    try {
        const steps = [
            ['node', ['tools/modules.js', 'configure', `--modules=${job.modules.join(',')}`]],
            ['make', runtime.make],
        ];
        for (const [command, args] of steps) {
            const code = await run(command, args, log);
            if (code !== 0) throw new Error(`${command} exited with ${code}`);
        }
        job.artifacts = [];
        for (const [name, source] of Object.entries(runtime.artifacts)) {
            const from = path.join(ATHENA_ROOT, source);
            if (!fs.existsSync(from)) continue;
            fs.copyFileSync(from, path.join(dir, name));
            job.artifacts.push(name);
        }
        if (!job.artifacts.length) throw new Error('the build produced no artifacts');
        job.status = 'done';
    } catch (err) {
        job.status = 'failed';
        job.error = err.message;
        log.write(`\n[build-server] ${err.message}\n`);
    } finally {
        job.finished = new Date().toISOString();
        await new Promise(resolve => log.end(resolve));
        writeStatus(job);
    }
}

async function pump() {
    if (running || !queue.length) return;
    const job = jobs.get(queue.shift());
    running = job;
    job.status = 'building';
    writeStatus(job);
    try {
        await build(job);
    } finally {
        running = null;
        setImmediate(pump);
    }
}

function submit(requested, runtimeName) {
    if (!Array.isArray(requested) || requested.some(id => typeof id !== 'string')) {
        return { code: 400, body: { error: '"modules" must be an array of module ids' } };
    }
    if (!RUNTIMES[runtimeName]) {
        return { code: 400, body: { error: `"runtime" must be one of: ${Object.keys(RUNTIMES).join(', ')}` } };
    }
    const unknown = requested.filter(id => !MODULE_IDS.has(id));
    if (unknown.length) {
        return { code: 400, body: { error: `unknown modules: ${unknown.join(', ')}` } };
    }

    let resolved;
    try {
        resolved = resolveDependencies(requested, MODULES).map(m => m.id).sort();
    } catch (err) {
        return { code: 400, body: { error: err.message } };
    }

    const id = crypto.createHash('sha256')
        .update(JSON.stringify([COMMIT, runtimeName, resolved]))
        .digest('hex');

    let job = jobs.get(id);
    if (!job) {
        const stored = readStatus(id);
        if (stored && stored.status === 'done') {
            job = stored;
            jobs.set(id, job);
        }
    }
    if (job && job.status !== 'failed') {
        return { code: job.status === 'done' ? 200 : 202, body: publicJob(job) };
    }
    if (queue.length >= MAX_QUEUE) {
        return { code: 503, body: { error: 'the build queue is full, try again later' } };
    }

    job = {
        id,
        status: 'queued',
        runtime: runtimeName,
        modules: resolved,
        requested: [...new Set(requested)].sort(),
        commit: COMMIT,
        created: new Date().toISOString(),
    };
    jobs.set(id, job);
    queue.push(id);
    writeStatus(job);
    setImmediate(pump);
    return { code: 202, body: publicJob(job) };
}

/* ------------------------------------------------------------------ */
/* HTTP                                                               */
/* ------------------------------------------------------------------ */

function send(res, code, body) {
    res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify(body));
}

function readBody(req) {
    return new Promise((resolve, reject) => {
        let size = 0;
        const chunks = [];
        req.on('data', chunk => {
            size += chunk.length;
            if (size > MAX_BODY) {
                reject(new Error('request body too large'));
                req.destroy();
                return;
            }
            chunks.push(chunk);
        });
        req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
        req.on('error', reject);
    });
}

function catalog() {
    return {
        commit: COMMIT,
        runtimes: Object.keys(RUNTIMES),
        modules: MODULES.map(m => ({
            id: m.id,
            name: m.name,
            required: !!m.required,
            default: !!m.default,
            dependencies: m.dependencies?.modules || [],
        })),
    };
}

const CONTENT_TYPES = {
    '.elf': 'application/octet-stream',
    '.gz': 'application/gzip',
    '.ts': 'text/plain; charset=utf-8',
    '.log': 'text/plain; charset=utf-8',
};

async function handle(req, res) {
    res.setHeader('Access-Control-Allow-Origin', ALLOWED_ORIGIN);
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    const url = new URL(req.url, 'http://localhost');
    const parts = url.pathname.split('/').filter(Boolean);

    if (req.method === 'GET' && url.pathname === '/api/catalog') {
        return send(res, 200, catalog());
    }

    if (req.method === 'POST' && url.pathname === '/api/builds') {
        let payload;
        try {
            payload = JSON.parse(await readBody(req));
        } catch (err) {
            return send(res, 400, { error: `invalid JSON body: ${err.message}` });
        }
        const result = submit(payload?.modules, payload?.runtime || 'quickjs');
        return send(res, result.code, result.body);
    }

    if (req.method === 'GET' && parts[0] === 'api' && parts[1] === 'builds' && ID_PATTERN.test(parts[2] || '')) {
        const id = parts[2];
        const job = jobs.get(id) || readStatus(id);
        if (!job) return send(res, 404, { error: 'unknown build' });

        if (parts.length === 3) return send(res, 200, publicJob(job));

        const name = parts[3];
        const allowed = name === 'build.log' || (job.artifacts || []).includes(name);
        const file = path.join(jobDir(id), name);
        if (parts.length !== 4 || !allowed || !fs.existsSync(file)) {
            return send(res, 404, { error: 'unknown artifact' });
        }
        res.writeHead(200, {
            'Content-Type': CONTENT_TYPES[path.extname(name)] || 'application/octet-stream',
            'Content-Disposition': `attachment; filename="${name}"`,
        });
        fs.createReadStream(file).pipe(res);
        return;
    }

    send(res, 404, { error: 'not found' });
}

http.createServer((req, res) => {
    handle(req, res).catch(err => {
        console.error(err);
        if (!res.headersSent) send(res, 500, { error: 'internal error' });
    });
}).listen(PORT, () => {
    console.log(`[build-server] AthenaEnv ${COMMIT.slice(0, 8)} at ${ATHENA_ROOT}`);
    console.log(`[build-server] ${MODULES.length} modules, cache in ${CACHE_DIR}, listening on :${PORT}`);
});
