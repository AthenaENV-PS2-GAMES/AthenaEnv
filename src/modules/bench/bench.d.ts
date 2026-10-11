/**
 * One synchronous benchmark batch per frame, timed by the native Profiler
 * clock. Setup, teardown, summary sorting and checkpoints are not timed.
 * Samples are milliseconds per invocation (batch time / iterations),
 * including timer/JS overhead; no overhead is subtracted. Include an empty
 * task as a reference for small kernels. Every batch must finish within
 * the EE clock's approximately 14.5-second wrap interval.
 * Draw tasks measure CPU submission; include an explicit GS wait in the
 * task to measure completion. Report metadata should identify platform,
 * build options, revision, scene and whether the task waits for the GS.
 * Not in the default build: configure with --modules=bench,usbmass.
 */
declare namespace Bench {
    interface Task<T = any> {
        name: string;
        setup?(): T;
        run(context: T, iteration: number): unknown;
        /** Called once even when setup/run fails; context may be undefined after a failed setup. */
        teardown?(context: T | undefined): void;
        /** Per-batch preparation outside the work timer, including warmup. */
        beforeBatch?(context: T): void;
        /** Runs even if beforeBatch/run throws or cancels; outside the work timer. */
        afterBatch?(context: T): void;
        /** Once before teardown, even on error/cancel. Snapshot counters/memory here. */
        inspect?(context: T | undefined): unknown;
        /** Scene, data sizes, phase boundaries and explicit GS-wait policy. */
        metadata?: Record<string, unknown>;
        warmup?: number;
        samples?: number;
        iterations?: number;
    }
    interface Result {
        name: string;
        status: "completed" | "error" | "cancelled";
        warmup: number; samples: number; iterations: number;
        averageMs: number; p95Ms: number; minMs: number; maxMs: number;
        metadata: Record<string, unknown>;
        /** Successful measured batches in execution order, without warmup or failed batches. */
        sampleTicks: number[];
        /** Work time per invocation, i.e. sampleTicks converted and divided by iterations. */
        sampleMs: number[];
        /** Total batch CPU time, including beforeBatch/afterBatch, never divided by iterations.
         * Excludes setup, inspect, teardown, checkpoints and the rest of the application frame.
         * This is a lower bound on frame CPU cost, not a total frame measurement. */
        batchCpu: {
            averageMs: number; p95Ms: number; minMs: number; maxMs: number;
            sampleMs: number[]; overBudget: number;
        };
        metrics?: unknown;
        error?: string;
    }
    interface Report {
        version: 2; label: string; metadata: Record<string, unknown>;
        frameBudgetMs: number;
        status: "running" | "completed" | "cancelled" | "error";
        results: Result[];
    }
    interface Options {
        label?: string;
        metadata?: Record<string, unknown>;
        /** Warmup batches, default 30; 0..10000. */
        warmup?: number;
        /** Measured batches, default 120; 1..10000. */
        samples?: number;
        /** Work invocations per batch, default 1; 1..10000. */
        iterations?: number;
        /** Total target frame budget in ms, default 1000/60. Used for batchCpu.overBudget.
         * Each batch AND its preparation/cleanup must finish before the EE clock wraps. */
        frameBudgetMs?: number;
        /** Rewrites JSON between tasks; writable device required, not an atomic save. */
        path?: string;
        onResult?(result: Result, report: Report): void;
    }
    class Runner {
        constructor(tasks: Task[], options?: Options);
        readonly report: Report;
        readonly done: boolean;
        /** Executes one batch; false when finished. Reentrant calls throw. */
        step(): boolean;
        /** Cleanup now, or after the current callback returns. */
        cancel(): void;
    }
    /**
     * Attaches to the existing Loop; it must be running to settle the promise.
     * Task failures resolve with an error report; checkpoint/onResult failures reject.
     */
    function run(tasks: Task[], options?: Options): Promise<Report>;
    /** Writes JSON using std.open; path/open/write errors throw. */
    function save(report: Report, path: string): void;
}
