/**
 * Distance-based level of detail for Scene3D nodes: forests, cities, crowds.
 *
 * A Group gives a node meshes by distance from the camera; beyond the last
 * level, or beyond the global draw distance, the node is hidden. Selection
 * runs in C, uses the node's world position of the last scene update (one
 * frame of latency) and touches the node only when its level changes, so it
 * must run before Scene3D's update: `setCamera()` adds a Loop POST_UPDATE
 * system at priority -100 (Scene3D's attachLoop() defaults to 0); games
 * without it call `LOD.update(camera)` before `scene.update()`. A
 * hysteresis band (default 10% of each threshold) stops flicker. While a
 * group is enabled it owns its node's `visible` flag. Keep Group handles
 * alive for as long as they are needed; disposing/collecting one removes it.
 *
 * Not in the default build: `node tools/modules.js configure --modules=lod,...`
 *
 * Example:
 * ```js
 * const groups = trees.map(tree => new LOD.Group(tree,
 *     [{ mesh: treeHigh, until: 15 }, { mesh: treeLow, until: 45 }, { mesh: billboard, until: 90 }]));
 * LOD.setCamera(camera);
 * LOD.setDrawDistance(90, { lights, color: [0.6, 0.7, 0.85] });   // fog hides the cut
 * ```
 */
declare namespace LOD {
    const MAX_LEVELS: 8;
    /** Group.level of a hidden node. */
    const HIDDEN: -1;
    interface Level {
        /** null hides the node in this band. */
        mesh: Model3D.Mesh | null;
        /** Used while the distance is below this (increasing, > 0). */
        until: number;
    }
    interface Stats { groups: number; hidden: number; changes: number; perLevel: number[] }
    class Group {
        /** Retains the node and meshes. hysteresis: 0 to 0.5 of each threshold (default 0.1). */
        constructor(node: Scene3D.Node, levels: Level[], options?: { hysteresis?: number });
        /** Current level, HIDDEN, or -2 before the first selection. */
        readonly level: number;
        /** Distance at the last selection. */
        readonly distance: number;
        /** Disabled groups leave the node alone. */
        enabled: boolean;
        dispose(): void;
    }
    /** Selects levels every frame with this camera (retained); null stops. */
    function setCamera(camera: Camera3D.Camera | null): void;
    /** Selects every level now; returns the number of changes, or fills stats. */
    function update(camera: Camera3D.Camera): number;
    function update<T extends object>(camera: Camera3D.Camera, stats: T): T & Stats;
    /** Of the last selection. */
    function stats<T extends object = Stats>(out?: T): T & Stats;
    /** Multiplies every threshold (quality setting, default 1). */
    function setBias(bias: number): void;
    /**
     * Hides nodes beyond distance (0: no limit). With `lights`, sets their
     * fog from fogStart (default 60%) to distance in `color`, so the cut
     * fades instead of popping.
     */
    function setDrawDistance(distance: number, options?: { lights?: Lights.Set; fogStart?: number; color?: [number, number, number] }): void;
}
