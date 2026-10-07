/** Rigid bodies in C: spheres, boxes and capsules that fall, bounce, slide, roll and
 * stack, against each other and against the static triangles of a
 * Collision3D world (the level). Sequential impulses with friction,
 * restitution and warm starting; bodies resting together fall asleep as one
 * island and wake when something touches them. Fixed 1/60 s substeps (at
 * most 4 per step; a slower frame rate drops the backlog). Up is wherever
 * gravity points against.
 *
 * ```js
 * const level = new Collision3D.World(); level.addNode(levelNode);
 * const physics = new Physics3D.World(level).attachLoop();
 * const crate = physics.addBox({ halfExtents: [.5, .5, .5], mass: 2, position: [0, 5, 0] });
 * crate.bind(crateNode);                       // position and rotation follow
 * crate.applyImpulse(0, 4, 2);
 * ```
 */
declare namespace Physics3D {
    /** Default attachLoop() priority: before Collision3D characters (-50) and Scene3D (0). */
    const LOOP_PRIORITY: number;
    const MAX_BODIES: number;
    const MAX_JOINTS: number;
    type Vec3 = [number, number, number] | Float32Array;
    interface WorldOptions {
        /** Default [0, -9.81, 0]. */
        gravity?: Vec3;
        /** Velocity iterations per substep, 1..64. Default 8. */
        iterations?: number;
        /** Layers of the level's triangles that bodies hit. Default -1. */
        staticMask?: number;
    }
    interface BodyOptions {
        /** Default "dynamic". Kinematic bodies move by their velocity and
         * push dynamic ones; static ones do not move. */
        type?: "dynamic" | "kinematic" | "static";
        /** Spheres and capsules. Default 0.5. */
        radius?: number;
        /** Capsules: half the length of the segment along the local y axis,
         * caps excluded (total height 2 * (halfHeight + radius)). Default 0.5. */
        halfHeight?: number;
        /** Boxes. Default [0.5, 0.5, 0.5]. */
        halfExtents?: Vec3;
        /** Dynamic bodies. Default 1. */
        mass?: number;
        position?: Vec3;
        /** Quaternion x, y, z, w. Default identity. */
        rotation?: [number, number, number, number] | Float32Array;
        velocity?: Vec3;
        angularVelocity?: Vec3;
        /** Default 0.5; pairs use the geometric mean. */
        friction?: number;
        /** 0..1, default 0; pairs use the larger. */
        restitution?: number;
        /** Spheres and capsules: rolling resistance (times the radius), so
         * they stop rolling. Default 0.02. */
        rollingFriction?: number;
        /** In 1/s. Defaults 0.05 and 0.1. */
        linearDamping?: number;
        angularDamping?: number;
        /** Bodies collide when each mask has the other's layer. Default 1 and -1. */
        layer?: number;
        mask?: number;
    }
    class World {
        /** statics: the level, or null. */
        constructor(statics?: Collision3D.World | null, options?: WorldOptions);
        addSphere(options?: BodyOptions): Body;
        addBox(options?: BodyOptions): Body;
        /** An upright capsule (local y axis); rotate it with `rotation`. */
        addCapsule(options?: BodyOptions): Body;
        /** Advances by dt; returns the substeps run. */
        step(dt: number): number;
        setGravity(x: number, y: number, z: number): this;
        readonly bodyCount: number;
        /** Contacts solved in the last substep. */
        readonly contactCount: number;
        readonly jointCount: number;
        /** Microseconds spent in the last step(), by phase. */
        readonly profile: { collide: number; prepare: number; solve: number; integrate: number; substeps: number };
        /** The anchor points of a and b stay together (b null: the world).
         * Points and axes are in world space at creation. */
        addBallJoint(a: Body, b: Body | null, anchor: Vec3): Joint;
        /** A ball joint that only turns about axis; limits in radians from
         * the creation pose, a motor driving the relative angular speed. */
        addHingeJoint(a: Body, b: Body | null, anchor: Vec3, axis: Vec3, options?: {
            lower?: number; upper?: number; motorSpeed?: number; maxMotorTorque?: number }): Joint;
        /** Keeps anchorA (on a) and anchorB (on b) at length (default: the
         * current distance); a rope only stops them from moving apart. */
        addDistanceJoint(a: Body, b: Body | null, anchorA: Vec3, anchorB: Vec3, options?: {
            length?: number; rope?: boolean }): Joint;
        /** Keeps the relative position and rotation of the creation pose. */
        addWeldJoint(a: Body, b: Body | null, anchor: Vec3): Joint;
        /** One native POST_UPDATE system stepping this world; idempotent. */
        attachLoop(priority?: number): this;
        detachLoop(): boolean;
        readonly attached: boolean;
        /** Detaches it; its bodies are removed when no handle keeps the world. */
        dispose(): void;
    }
    /** Removed with either of its bodies. At least one body is dynamic.
     * Bodies joined by a joint do not collide with each other. */
    class Joint {
        private constructor();
        readonly type: "ball" | "hinge" | "distance" | "weld";
        readonly alive: boolean;
        /** Hinges: the angle of a relative to b about the axis (last step). */
        readonly angle: number;
        /** Hinges: lower <= upper, radians. */
        setLimits(lower: number, upper: number): this;
        disableLimits(): this;
        /** Hinges: target relative angular speed (rad/s) with at most
         * maxTorque; maxTorque 0 turns the motor off. */
        setMotor(speed: number, maxTorque: number): this;
        remove(): void;
        dispose(): void;
    }
    class Body {
        private constructor();
        readonly x: number;
        readonly y: number;
        readonly z: number;
        vx: number; vy: number; vz: number;
        /** Angular velocity, rad/s. */
        wx: number; wy: number; wz: number;
        readonly type: "dynamic" | "kinematic" | "static";
        readonly sleeping: boolean;
        /** False once removed from its world. */
        readonly alive: boolean;
        /** Quaternion [x, y, z, w]; fills out when given. */
        getRotation(out?: number[] | Float32Array): number[];
        setPosition(x: number, y: number, z: number): this;
        setRotation(x: number, y: number, z: number, w: number): this;
        setVelocity(x: number, y: number, z: number): this;
        setAngularVelocity(x: number, y: number, z: number): this;
        /** Mass * velocity, at a world point (default the centre). */
        applyImpulse(x: number, y: number, z: number, px?: number, py?: number, pz?: number): this;
        /** For the next step only. */
        applyForce(x: number, y: number, z: number): this;
        wake(): this;
        /** A node whose position and rotation follow the body; null unbinds. */
        bind(node: Scene3D.Node | null): this;
        /** Takes the body out of its world. */
        remove(): void;
        /** remove(), and drops this handle. */
        dispose(): void;
    }
}
