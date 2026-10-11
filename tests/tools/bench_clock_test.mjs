// Deterministic clocks verify units, wrap, sample order and budget accounting.
import { readFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
const data = source => 'data:text/javascript;base64,' + Buffer.from(source).toString('base64');
const clockURL = data('let now = 0; export function set(v) { now = v >>> 0; } export function advance(v) { now = (now + v) >>> 0; } export function ticks() { return now; } export function ticksToMilliseconds(...args) { if (args.length !== 1) throw new TypeError("native timer arity"); return args[0] / 1000; }');
const clock = await import(clockURL);
const loopURL = data('export function addSystem() {} export function removeSystem() {}');
const source = (await readFile(new URL('../../src/modules/bench/js/bench.js', import.meta.url), 'utf8'))
    .replace('"Loop"', JSON.stringify(loopURL)).replace('"ProfilerNative"', JSON.stringify(clockURL));
const { Runner } = await import(data(source));
const work = [9000, 1000, 3000];
let batch = 0;
clock.set(0xfffffff0);
const runner = new Runner([{
    name: 'wrap', setup() { clock.advance(100000); },
    beforeBatch() { clock.advance(2000); },
    run() { clock.advance(work[batch]); },
    afterBatch() { clock.advance(4000); batch++; },
    inspect() { clock.advance(100000); return { batches: batch }; },
    teardown() { clock.advance(100000); },
}], { warmup: 1, samples: 2, iterations: 2, frameBudgetMs: 10 });
while (runner.step()) {}
const r = runner.report.results[0];
assert.deepEqual(r.sampleTicks, [2000, 6000]);
assert.deepEqual(r.sampleMs, [1, 3]);
assert.equal(r.averageMs, 2);
assert.equal(r.p95Ms, 3);
assert.deepEqual(r.batchCpu.sampleMs, [8, 12]);
assert.equal(r.batchCpu.averageMs, 10);
assert.equal(r.batchCpu.overBudget, 1);
assert.deepEqual(r.metrics, { batches: 3 });
// Wrap specifically inside work, without setup shifting it away.
clock.set(0xfffffff0);
const wrap = new Runner([{ name: 'wrap-work', run() { clock.advance(32); } }], { warmup: 0, samples: 1 });
wrap.step();
assert.equal(wrap.report.results[0].sampleTicks[0], 32);
assert.equal(wrap.report.results[0].sampleMs[0], .032);
console.log('Bench deterministic clock tests passed');
