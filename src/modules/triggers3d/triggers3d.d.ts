/**
 * Trigger volumes: checkpoints, doors, damage zones, music changes, area
 * loading. Zones (boxes and spheres) report bodies (spheres; radius 0 is a
 * point) that enter and leave them. `update()` tests every pair in C and
 * runs JavaScript only for the changes. A zone reacts to the bodies whose
 * `layers` share a bit with its `mask`. Zones and bodies can follow a
 * Scene3D node: they use its world position of the last scene update, so
 * call `update()` after `scene.update()` (or let the attached Loop update the
 * scene first).
 *
 * Stay: `onStay(body, zone)` (opt-in per zone by passing or assigning a function, or world-wide with
 * `world.onStay`) runs every update for each body that was already inside at the previous update and
 * still is; a body that enters in an update gets ENTER only, STAY from the next one. Per update the
 * callbacks run in this order: every ENTER/EXIT (by zone id, then body id), then every STAY (same order).
 * Each STAY is one JavaScript call, so opt in only the zones that need it.
 *
 * Many zones and bodies: `world.setGrid(cellSize)` adds a broad phase (bodies binned by their bounds; each
 * zone tests only the bodies of the cells it covers; still bodies cost nothing per update; a body that
 * leaves a zone's neighbourhood still gets its EXIT). Events and their order are identical with and
 * without it; `world.stats()` shows how many pairs were considered. Not in the default build: `node tools/modules.js configure --modules=triggers3d,...`
 *
 * Example:
 * ```js
 * const triggers = new Triggers3D.World();
 * const PLAYER = 1, ENEMY = 2;
 * const hero = triggers.body(0, 0, 0, { radius: 0.4, layers: PLAYER }).follow(heroNode);
 * triggers.box(10, 0, -2, 12, 3, 2, { mask: PLAYER, onEnter: () => openDoor() });
 * triggers.sphere(0, 0, 20, 3, { mask: PLAYER | ENEMY, onEnter: b => hurt(b.data), onExit: b => stopHurt(b.data) });
 * // each frame, after the scene update:
 * triggers.update();
 * ```
 */
declare namespace Triggers3D {
    /** Zones and bodies per world, each. */
    const MAX: number;
    interface ZoneOptions {
        /** Body layers it reacts to (default all bits). */
        mask?: number;
        onEnter?(body: Body, zone: Zone): void;
        onExit?(body: Body, zone: Zone): void;
        /** Every update while a body stays inside (opt-in). */
        onStay?(body: Body, zone: Zone): void;
        data?: any;
    }
    class Zone {
        private constructor();
        readonly id: number;
        enabled: boolean;
        mask: number;
        onEnter?: (body: Body, zone: Zone) => void;
        onExit?: (body: Body, zone: Zone) => void;
        /** Assigning a function turns STAY events on for this zone; undefined turns them off. */
        onStay?: (body: Body, zone: Zone) => void;
        data: any;
        setBox(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number): this;
        setSphere(x: number, y: number, z: number, radius: number): this;
        /** Moves with the node plus the offset (the shape's coordinates become relative); null stops. Retains the node. */
        follow(node: Scene3D.Node | null, ox?: number, oy?: number, oz?: number): this;
        /** As of the last update(). */
        contains(body: Body): boolean;
        occupants(): Body[];
        /** Removes the zone; onExit runs for the bodies inside. */
        dispose(): void;
    }
    class Body {
        private constructor();
        readonly id: number;
        readonly radius: number;
        layers: number;
        data: any;
        setPosition(x: number, y: number, z: number, radius?: number): this;
        /** The position becomes the node's world position plus the offset; null stops. */
        follow(node: Scene3D.Node | null, ox?: number, oy?: number, oz?: number): this;
        /** Removes the body; onExit runs for the zones it was in. */
        dispose(): void;
    }
    class World {
        constructor();
        /** Called for every event after the zone's own callbacks. */
        onEnter?: (zone: Zone, body: Body) => void;
        onExit?: (zone: Zone, body: Body) => void;
        /** STAY for every zone, after the zone's own callbacks (opt-in: assigning a function turns it on). */
        onStay?: (zone: Zone, body: Body) => void;
        /**
         * Broad phase: cellSize near the typical zone size; 0 turns it off (default). `capacity` (default 4096)
         * bounds the body-cell entries: bodies that do not fit, or cover more than 64 cells, are tested against
         * every zone; zones covering more than 256 cells test every body.
         */
        setGrid(cellSize: number, options?: { capacity?: number }): this;
        /** Of the last update(): pairs considered and tested, events, occupied pairs and grid occupancy. */
        stats(): { zones: number; bodies: number; candidates: number; tested: number; events: number; stayEvents: number;
            occupiedPairs: number; dirtyBodies: number; cells: number; entries: number; largeBodies: number; largeZones: number };
        box(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number, options?: ZoneOptions): Zone;
        sphere(x: number, y: number, z: number, radius: number, options?: ZoneOptions): Zone;
        body(x: number, y: number, z: number, options?: { radius?: number; layers?: number; data?: any }): Body;
        /**
         * Tests the pairs and runs callbacks; returns events generated (ENTER, EXIT and STAY).
         * Callbacks may remove/create bodies or zones: pending events for
         * removed objects are skipped, new objects are tested next update.
         * Recursive update() throws; callback errors propagate.
         */
        update(): number;
        zones(): Zone[];
        bodies(): Body[];
        /** Releases native state and invalidates all its zones/bodies; no exit callbacks. */
        dispose(): void;
    }
}
