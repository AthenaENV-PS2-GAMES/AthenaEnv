/**
 * Navigation for enemies, NPCs and mobs: A* on an XZ grid and crowds of
 * agents, in C.
 *
 * A Grid covers width x depth cells of `cellSize` world units from (x, z);
 * each cell has a cost: 0 is blocked, 1-255 multiplies the distance walked
 * through it (mud, water...). Every cell starts at 1. `findPath` runs A*
 * over fixed arrays (no allocation per search) with 8-way moves that never
 * cut corners, and by default smooths the path by line of sight through
 * cost-1 cells, retaining detours around more expensive terrain. Native
 * searches reuse scratch storage; findPath() allocates its returned array.
 *
 * A Crowd moves agents along their paths every `update(dt)`, slowing down
 * on arrival and pushing overlapping agents apart (staying on walkable
 * cells); an agent bound to a Scene3D node sets its position (keeping the
 * agent's y) and, with `face`, its yaw.
 *
 * Not in the default build: `node tools/modules.js configure --modules=nav,...`
 *
 * Example:
 * ```js
 * const grid = new Nav.Grid(64, 64, { cellSize: 1, x: -32, z: -32 });
 * grid.fill(10, 0, 12, 40, 0);                         // a wall
 * const crowd = new Nav.Crowd(grid);
 * const orc = crowd.add({ x: 0, y: 0, z: 0, speed: 3, node: orcNode });
 * orc.moveTo(player.x, player.z);
 * // each frame:
 * crowd.update(dt);
 * ```
 */
declare namespace Nav {
    /** Cells per grid (512 x 512). */
    const MAX_CELLS: number;
    /** Points per path. */
    const MAX_PATH: number;
    const MAX_AGENTS: number;
    interface PathOptions {
        /** 8-way moves (default true). */
        diagonal?: boolean;
        /** Shortcuts across visible cost-1 cells (default true); preserves weighted detours. */
        smooth?: boolean;
        /** Integer 0..MAX_CELLS; 0/default searches the whole grid.
         * Exhaustion returns null in findPath, or iterationLimit in a Request. */
        maxIterations?: number;
    }
    class Grid {
        constructor(width: number, depth: number, options?: { cellSize?: number; x?: number; z?: number });
        readonly width: number; readonly depth: number; readonly cellSize: number;
        /** Cells expanded by the last findPath (its cost). */
        readonly lastExpanded: number;
        setCost(cellX: number, cellZ: number, cost: number): this;
        /** 0 outside the grid. */
        getCost(cellX: number, cellZ: number): number;
        /** Inclusive cell rectangle; returns the cells set. */
        fill(x0: number, z0: number, x1: number, z1: number, cost: number): number;
        /** width * depth costs, x fastest. */
        setCosts(costs: Uint8Array): this;
        /** True when every cell the segment crosses is walkable. */
        lineOfSight(x0: number, z0: number, x1: number, z1: number): boolean;
        /** World x, z pairs from the start to the exact target, or null when unreachable. */
        findPath(x0: number, z0: number, x1: number, z1: number, options?: PathOptions): Float32Array | null;
        /** FIFO incremental query; allocates request/output at creation.
         * At most MAX_PENDING pending queries. Throws on full queue/invalid input.
         * findPath and Crowd Agent.moveTo throw while this queue is nonempty;
         * moveTo preserves the agent's previous path on that conflict. */
        requestPath(x0: number, z0: number, x1: number, z1: number, options?: RequestOptions): Request;
        /** Advances the FIFO, including initialization, A*, reconstruction and smoothing.
         * maxWork defaults to 256 (integer 1..MAX_CELLS); budgetMs defaults to 0
         * (no clock limit), otherwise finite, positive and <=1000. One work unit
         * is an expansion, trace/output point or smoothing cell, not just A*.
         * Stops at either limit; at most one unit plus call overhead may exceed
         * the CPU budget. Returns work units, with no native allocation. */
        stepRequests(maxWork?: number, budgetMs?: number): number;
        /** Cancels all pending requests; completed results stay valid. */
        cancelRequests(): void;
        readonly pendingRequests: number;
        /** Changes only when costs change; mutations invalidate pending requests. */
        readonly revision: number;
        /** Centre of the nearest walkable cell within radius cells (default 8), or null. */
        nearestWalkable(x: number, z: number, radius?: number): { x: number; z: number } | null;
        /** Cancels pending requests; retained Crowds and completed results remain safe. */
        dispose(): void;
    }
    const MAX_PENDING: number;
    interface RequestOptions extends PathOptions {
        /** Owned result capacity in points (2..MAX_CELLS+1), default MAX_PATH.
         * Native output uses 8 bytes per point; the six grid arrays are shared.
         * Exceeding this capacity produces error pathTooLong, with no partial result. */
        maxPoints?: number;
    }
    class Request {
        private constructor();
        readonly state: "pending" | "found" | "noPath" | "cancelled" | "error";
        readonly phase: "queued" | "initialize" | "clearStamps" | "search" | "countPath" | "tracePath" | "emitPath" | "smoothPath" | "finished";
        readonly error: "gridChanged" | "pathTooLong" | "iterationLimit" | "invalid" | null;
        readonly revision: number;
        readonly expanded: number;
        readonly workUnits: number;
        readonly pointCount: number;
        /** Native request state plus owned output, excluding allocator and JS overhead. */
        readonly storageBytes: number;
        /** Weighted A* cost in cell units; 0 unless found. */
        readonly cost: number;
        /** Allocates an owned JS result, or null unless found. Outside step's budget. */
        getPath(): Float32Array | null;
        /** Writes to reusable storage (honors subarray), returns valid float length
         * pointCount * 2, or 0 unless found. Insufficient capacity throws before
         * any write. Copying is outside step's budget. */
        copyPath(out: Float32Array): number;
        cancel(): void;
        /** Cancels/unlinks if pending (including an agent's owned plan), then
         * releases this observer's output/grid reference. Idempotent.
         * GC only drops ownership: an agent's plan continues without an observer. */
        dispose(): void;
    }
    class Agent {
        private constructor();
        readonly x: number; readonly y: number; readonly z: number;
        readonly state: "idle" | "moving" | "arrived" | "planning";
        /** Last incremental outcome; synchronous moveTo resets it to none.
         * Movement/separation pause while pending to preserve the search start. */
        readonly planningState: "none" | "pending" | "found" | "noPath" | "cancelled" | "error";
        readonly planningError: Request["error"];
        /** Facing angle about Y (radians), from the last movement. */
        readonly yaw: number;
        /** Distance per second of the last update. */
        readonly velocity: number;
        /** Points of the current path. */
        readonly waypoints: number;
        /** Units per second. */
        speed: number;
        /** Plans a path; false when unreachable (the agent stops). */
        moveTo(x: number, z: number): boolean;
        /** Plans incrementally using this Crowd's PathOptions. Advance once per
         * frame with grid.stepRequests(); crowd.update(dt) only moves agents.
         * Successful creation abandons the previous route and enters planning.
         * Found paths are adopted during step (bounded copy <=MAX_PATH points);
         * other outcomes leave the agent idle. Creation failure preserves motion.
         * Replaces its previous plan at the FIFO tail, including at queue capacity.
         * The agent owns planning even if the returned Request is garbage-collected.
         * Request.cancel/dispose, stop/setPosition, Agent.dispose/GC, Crowd.dispose
         * and grid changes cancel/invalidate pending plans. */
        requestMoveTo(x: number, z: number): Request;
        stop(): this;
        setPosition(x: number, y: number, z: number): this;
        /** Drives a node (retained); null stops. */
        bind(node: Scene3D.Node | null): this;
        /** Cancels planning and leaves the crowd. */
        dispose(): void;
    }
    class Crowd {
        /** options: the PathOptions used by moveTo() and snapshotted by requestMoveTo(). */
        constructor(grid: Grid, options?: PathOptions);
        add(options: { x?: number; y?: number; z?: number; speed?: number; radius?: number; face?: boolean; node?: Scene3D.Node }): Agent;
        /** Moves every agent; returns how many arrived in this update. */
        update(dt: number): number;
        /** Cancels this crowd's pending plans; retained Agents remain usable. */
        dispose(): void;
    }
}
