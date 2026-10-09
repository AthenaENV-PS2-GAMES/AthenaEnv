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
        /** Integer 0..MAX_CELLS; 0/default searches the whole grid. Exhaustion returns null. */
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
        /** Centre of the nearest walkable cell within radius cells (default 8), or null. */
        nearestWalkable(x: number, z: number, radius?: number): { x: number; z: number } | null;
        dispose(): void;
    }
    class Agent {
        private constructor();
        readonly x: number; readonly y: number; readonly z: number;
        readonly state: "idle" | "moving" | "arrived";
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
        stop(): this;
        setPosition(x: number, y: number, z: number): this;
        /** Drives a node (retained); null stops. */
        bind(node: Scene3D.Node | null): this;
        /** Leaves the crowd. */
        dispose(): void;
    }
    class Crowd {
        /** options: the PathOptions used by moveTo(). */
        constructor(grid: Grid, options?: PathOptions);
        add(options: { x?: number; y?: number; z?: number; speed?: number; radius?: number; face?: boolean; node?: Scene3D.Node }): Agent;
        /** Moves every agent; returns how many arrived in this update. */
        update(dt: number): number;
        dispose(): void;
    }
}
