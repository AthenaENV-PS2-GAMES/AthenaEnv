/** Right-handed, normalized xyzw rotation. Angles are radians.
 * Mutating methods return this; operands are unchanged; dispose is idempotent. */
declare namespace Quaternion {
    class Quaternion {
        constructor();
        constructor(x: number, y: number, z: number, w: number);
        setAxisAngle(x: number, y: number, z: number, radians: number): this;
        /** Sets this = this * other. */
        multiply(other: Quaternion): this;
        /** Shortest path toward other, t in [0,1]. */
        slerp(other: Quaternion, t: number): this;
        /** Independent snapshot. */
        toArray(): [number, number, number, number];
        dispose(): void;
    }
}
