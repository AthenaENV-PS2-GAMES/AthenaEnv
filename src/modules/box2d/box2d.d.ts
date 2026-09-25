/**
 * 2D rigid body physics with Box2D 3.2.
 *
 * A `World` owns `Body` objects; bodies carry `Shape`s (circle, box,
 * polygon, capsule, segment) and `Chain`s, and are connected by `Joint`s.
 * Units are meters, kilograms, seconds and radians; keep moving objects
 * between 0.1 and 10 meters and scale to pixels when drawing.
 *
 * Every Box2D object has exactly one script object: `shape.getBody() === body`,
 * and the shapes reported by queries and events can be compared with `===`
 * or used as Map keys. Objects hold their world (`body.world`), so the world
 * stays alive while any of them is reachable; an unreachable world is freed
 * by the GC with everything in it. `destroy()` frees the Box2D object at
 * once: later calls on it throw a TypeError (`isValid()` returns false).
 *
 * Invalid arguments throw instead of reaching Box2D, whose assertions would
 * reset the console: TypeError for wrong types or objects, RangeError for
 * values out of range (NaN and Infinity included). Every number must be
 * finite and within +-1e10; positions, points, distances and vectors
 * (`{ x, y }`) within +-100000 m (Box2D's B2_HUGE).
 *
 * Creating worlds, bodies, shapes, chains or joints, and `world.step()`,
 * throw a RangeError when Box2D would exceed `setMemoryLimit()` or leave less
 * than 256 KB of free RAM: Box2D itself cannot recover from a failed
 * allocation. A refused step leaves the world as it was. The limit goes back
 * to 0 (none) when the VM restarts (`std.reload()`).
 *
 * Performance: every getter allocates an object. To sync many sprites per
 * frame use `world.readTransforms(bodies, float32Array)` (no allocation) or
 * `world.getBodyEvents()` (moved bodies only).
 *
 * Example:
 * ```js
 * const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });
 *
 * const ground = world.createBody({ position: { x: 0, y: -1 } });
 * ground.createBoxShape({ halfWidth: 20, halfHeight: 1 });
 *
 * const crate = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 0, y: 5 } });
 * crate.createBoxShape({ halfWidth: 0.5, halfHeight: 0.5, friction: 0.4, enableContactEvents: true });
 *
 * while (true) {
 *     world.step(1 / 60, 4);
 *     for (const hit of world.getContactEvents().begin) {
 *         if (hit.shapeA.getBody() === crate || hit.shapeB.getBody() === crate) console.log("landed");
 *     }
 *     const t = crate.getTransform();
 *     drawCrate(t.x * PIXELS_PER_METER, t.y * PIXELS_PER_METER, t.angle);
 *     Screen.flip();
 * }
 * ```
 */
declare namespace Box2D {
    /** Box2D library version, `"3.2.0"`. */
    const version: string;
    /** Body types for `BodyOptions.type` and `Body.setType()`. */
    const STATIC_BODY: 0;
    const KINEMATIC_BODY: 1;
    const DYNAMIC_BODY: 2;
    /** Most vertices a polygon (or query proxy) can have: 8. */
    const MAX_POLYGON_VERTICES: number;
    /** Most worlds alive at a time (8 on the EE). */
    const MAX_WORLDS: number;

    /**
     * Caps the bytes Box2D may hold (0 = no limit, the default). Reset to 0
     * when the VM restarts.
     */
    function setMemoryLimit(bytes: number): void;
    function getMemoryLimit(): number;
    /** Bytes currently allocated by Box2D (all worlds). */
    function getMemoryUsage(): number;

    /**
     * Character mover: given the desired translation and the planes from
     * `world.collideMover()`, returns the translation that respects them.
     * Move the capsule by it, then collide again next frame.
     */
    function solvePlanes(dx: number, dy: number, planes: CollisionPlane[]): { x: number; y: number; iterations: number };
    /**
     * Removes the parts of a velocity that go into the planes. Pass the same
     * planes, after `solvePlanes` wrote their `push`.
     */
    function clipVector(vx: number, vy: number, planes: CollisionPlane[]): Vec2;

    /** A plane for the mover solver; `collideMover()` results can be passed as they are. */
    interface CollisionPlane {
        normal: Vec2;
        offset: number;
        /** Most the solver may push along this plane. Default: unlimited. */
        pushLimit?: number;
        /** `clipVector` clips against this plane. Default true (false for soft collision). */
        clipVelocity?: boolean;
        /** Written by `solvePlanes`: `clipVector` only clips against planes that pushed. */
        push?: number;
    }

    type BodyType = 0 | 1 | 2;
    type ShapeType = 'circle' | 'capsule' | 'segment' | 'polygon' | 'chainSegment';
    type JointType = 'distance' | 'filter' | 'motor' | 'prismatic' | 'revolute' | 'weld' | 'wheel';

    interface Vec2 {
        x: number;
        y: number;
    }

    interface AABB {
        lowerX: number;
        lowerY: number;
        upperX: number;
        upperY: number;
    }

    /**
     * 64-bit collision bits. Accepted as an integer number (a negative number
     * is two's complement: `-1` is every bit) or a BigInt. Read back as a
     * number within +-2^53, as a BigInt beyond (bits 53 to 62).
     */
    type Bits = number | bigint;

    /**
     * Two shapes collide when each one's categoryBits match the other's
     * maskBits, unless they share a groupIndex: positive always collides,
     * negative never does.
     */
    interface Filter {
        /** Default 1. */
        categoryBits?: Bits;
        /** Default -1 (every category). */
        maskBits?: Bits;
        /** Default 0. */
        groupIndex?: number;
    }

    /** Filter for queries: shapes whose categoryBits match maskBits, and whose maskBits match categoryBits. */
    interface QueryFilter {
        /** Default 1. */
        categoryBits?: Bits;
        /** Default -1. */
        maskBits?: Bits;
    }

    interface WorldOptions {
        /** Default `{ x: 0, y: -10 }`. */
        gravity?: Vec2;
        /** Let resting bodies sleep. Default true. */
        enableSleep?: boolean;
        /** Continuous collision between dynamic and static bodies. Default true. */
        enableContinuous?: boolean;
        /** Speed (m/s) below which collisions do not bounce. Default 1. */
        restitutionThreshold?: number;
        /** Approach speed (m/s) that reports a hit event. Default 1. */
        hitEventThreshold?: number;
        /** Speed cap in m/s, > 0. Default 400. */
        maximumLinearSpeed?: number;
        /** Accepted for compatibility (1..32); the EE always simulates on one worker. */
        workerCount?: number;
        /** Contact stiffness in Hz, >= 0. Default 30. See `World.setContactTuning()`. */
        contactHertz?: number;
        /** Contact damping ratio, >= 0. Default 10. */
        contactDampingRatio?: number;
        /** Most speed (m/s) at which overlap is pushed out, >= 0. Default 3. */
        contactSpeed?: number;
        /** Softer contacts between bodies of very different mass (experimental). Default false. */
        enableContactSoftening?: boolean;
        userData?: any;
    }

    interface BodyOptions {
        /** Default STATIC_BODY. */
        type?: BodyType;
        position?: Vec2;
        /** Radians. `rotation` is an alias. */
        angle?: number;
        rotation?: number;
        linearVelocity?: Vec2;
        angularVelocity?: number;
        /** >= 0. */
        linearDamping?: number;
        /** >= 0. */
        angularDamping?: number;
        /** Multiplies the world gravity; may be negative. Default 1. */
        gravityScale?: number;
        /** Speed below which the body may sleep, >= 0. Default 0.05 m/s. */
        sleepThreshold?: number;
        /** Stops rotation. */
        fixedRotation?: boolean;
        /** Blocks motion along world x (e.g. a 2D side view with vertical-only motion). */
        lockLinearX?: boolean;
        /** Blocks motion along world y. */
        lockLinearY?: boolean;
        /** Continuous collision against other dynamic bodies too (fast projectiles). */
        isBullet?: boolean;
        enableSleep?: boolean;
        isAwake?: boolean;
        isEnabled?: boolean;
        /** Lets the body spin past Box2D's rotation speed cap (round wheels only). Default false. */
        allowFastRotation?: boolean;
        /**
         * Reuse contact points between steps. Default true (faster); turn it
         * off for characters that must not catch on ghost collisions.
         */
        enableContactRecycling?: boolean;
        /** Debug name, cut to 10 bytes (B2_NAME_LENGTH) at a character boundary. */
        name?: string;
        userData?: any;
    }

    /** Surface properties of a shape; see `Shape.setSurfaceMaterial()`. */
    interface SurfaceMaterial {
        /** >= 0. Default 0.6. */
        friction?: number;
        /** Bounciness, >= 0 (above 1 gains energy). Default 0. */
        restitution?: number;
        /** >= 0. Default 0. */
        rollingResistance?: number;
        /** Conveyor belt speed along the surface. Default 0. */
        tangentSpeed?: number;
        /** Your own material id (e.g. for footstep sounds). Not used by Box2D. Default 0. */
        userMaterialId?: Bits;
        /** Debug draw color 0xRRGGBB; 0 = the default color. */
        customColor?: number;
    }

    /** Mass properties. `center` is local; the inertia is about the center of mass. */
    interface MassData {
        mass: number;
        center: Vec2;
        rotationalInertia: number;
    }

    /** Options shared by every `create*Shape` method. */
    interface ShapeOptions extends SurfaceMaterial {
        /** kg/m^2, >= 0. Default 1. */
        density?: number;
        /** Detects overlaps without colliding; see `World.getSensorEvents()`. */
        isSensor?: boolean;
        /** Report this shape to sensors (visitors need it too). Default false. */
        enableSensorEvents?: boolean;
        /** Contact begin/end events. Default false. */
        enableContactEvents?: boolean;
        /** Hit events above `hitEventThreshold`. Default false. */
        enableHitEvents?: boolean;
        filter?: Filter;
        userData?: any;
    }

    interface CircleOptions extends ShapeOptions {
        /** > 0. */
        radius: number;
        /** Local center. Default `{ x: 0, y: 0 }`. */
        center?: Vec2;
    }

    interface BoxOptions extends ShapeOptions {
        /** > 0. */
        halfWidth: number;
        /** > 0. */
        halfHeight: number;
        /** Local center. */
        center?: Vec2;
        /** Local rotation in radians. */
        angle?: number;
    }

    interface PolygonOptions extends ShapeOptions {
        /** 3 to 8 local points; their convex hull is used. */
        vertices: Vec2[];
        /** Rounds the corners, >= 0. Default 0. */
        radius?: number;
    }

    interface CapsuleOptions extends ShapeOptions {
        /** Local centers of the two half circles; more than 0.005 apart. */
        point1: Vec2;
        point2: Vec2;
        /** > 0. */
        radius: number;
    }

    interface SegmentOptions extends ShapeOptions {
        /** Local end points; more than 0.005 apart. */
        point1: Vec2;
        point2: Vec2;
    }

    interface ChainOptions extends SurfaceMaterial {
        /** Connects the last point to the first. Default false. */
        isLoop?: boolean;
        enableSensorEvents?: boolean;
        filter?: Filter;
        userData?: any;
    }

    interface JointOptions {
        /** Let the two bodies collide with each other. Default false. */
        collideConnected?: boolean;
        /** Constraint force (N) above which the joint reports a joint event. Default: never. */
        forceThreshold?: number;
        /** Constraint torque (N·m) above which the joint reports a joint event. Default: never. */
        torqueThreshold?: number;
        /** Stiffness of the joint constraint in Hz, >= 0 (advanced). Default 60. */
        constraintHertz?: number;
        /** Damping of the joint constraint, >= 0 (advanced). Default 2. */
        constraintDampingRatio?: number;
        userData?: any;
    }

    /**
     * `anchor` is a world point where both bodies are joined. Without it,
     * each body is joined at its own origin. Joints start relaxed: the
     * current relative angle is the zero angle of revolute joints and is
     * kept by weld joints.
     */
    interface AnchorOptions extends JointOptions {
        anchor?: Vec2;
    }

    interface SpringOptions {
        enableSpring?: boolean;
        /** Stiffness in Hz, >= 0. */
        hertz?: number;
        /** >= 0 (1 = critically damped). */
        dampingRatio?: number;
    }

    interface DistanceJointOptions extends JointOptions, SpringOptions {
        /** World point used by both bodies. */
        anchor?: Vec2;
        /** Local point on body A (when `anchor` is not given). */
        anchorA?: Vec2;
        /** Local point on body B (when `anchor` is not given). */
        anchorB?: Vec2;
        /** Rest length, > 0. Default: the current distance between the anchors. */
        length?: number;
        enableLimit?: boolean;
        minLength?: number;
        maxLength?: number;
        enableMotor?: boolean;
        maxMotorForce?: number;
        motorSpeed?: number;
        /**
         * Spring force range (N), lower <= upper: lower bounds the tension, upper
         * the compression (upperSpringForce 0 makes a spring that only pulls).
         * Default unlimited.
         */
        lowerSpringForce?: number;
        upperSpringForce?: number;
    }

    interface RevoluteJointOptions extends AnchorOptions, SpringOptions {
        enableLimit?: boolean;
        /** Radians, within +-0.99 * PI, lowerAngle <= upperAngle. */
        lowerAngle?: number;
        upperAngle?: number;
        enableMotor?: boolean;
        motorSpeed?: number;
        maxMotorTorque?: number;
        /** Angle (radians) the spring pulls towards. Default 0. */
        targetAngle?: number;
    }

    interface PrismaticJointOptions extends AnchorOptions, SpringOptions {
        /** World direction of the slide at creation. Default `{ x: 1, y: 0 }`. */
        axis?: Vec2;
        enableLimit?: boolean;
        lowerTranslation?: number;
        upperTranslation?: number;
        enableMotor?: boolean;
        motorSpeed?: number;
        maxMotorForce?: number;
        /** Translation (m) the spring pulls towards. Default 0. */
        targetTranslation?: number;
    }

    interface WeldJointOptions extends AnchorOptions {
        /** 0 = rigid. */
        linearHertz?: number;
        angularHertz?: number;
        linearDampingRatio?: number;
        angularDampingRatio?: number;
    }

    interface WheelJointOptions extends AnchorOptions, SpringOptions {
        /** World direction of the suspension at creation. Default `{ x: 0, y: 1 }`. */
        axis?: Vec2;
        enableLimit?: boolean;
        lowerTranslation?: number;
        upperTranslation?: number;
        enableMotor?: boolean;
        motorSpeed?: number;
        maxMotorTorque?: number;
    }

    /** Drives body B relative to body A (top-down friction, animated platforms). */
    interface MotorJointOptions extends JointOptions {
        linearVelocity?: Vec2;
        angularVelocity?: number;
        maxVelocityForce?: number;
        maxVelocityTorque?: number;
        linearHertz?: number;
        linearDampingRatio?: number;
        maxSpringForce?: number;
        angularHertz?: number;
        angularDampingRatio?: number;
        maxSpringTorque?: number;
    }

    interface Contact {
        shapeA: Shape;
        shapeB: Shape;
        /** From shape A to shape B. */
        normal: Vec2;
        /** 1 or 2 points. */
        points: { point: Vec2; separation: number; normalImpulse: number }[];
    }

    interface CastHit {
        shape: Shape;
        point: Vec2;
        normal: Vec2;
        /** Position along the cast, 0 (start) to 1 (end). */
        fraction: number;
    }

    interface MoverPlane {
        shape: Shape;
        /** Plane normal, pointing out of the shape. */
        normal: Vec2;
        /** Plane offset, relative to the mover origin. */
        offset: number;
        /** Contact point on the shape, relative to the mover origin. */
        point: Vec2;
    }

    /**
     * Events of the last step. A shape destroyed since then reads as null:
     * always check `end` events, and the others when shapes are destroyed
     * between `step()` and the read.
     */
    interface ContactEvents {
        begin: { shapeA: Shape; shapeB: Shape }[];
        end: { shapeA: Shape | null; shapeB: Shape | null }[];
        hit: { shapeA: Shape; shapeB: Shape; point: Vec2; normal: Vec2; approachSpeed: number }[];
    }

    /** See ContactEvents about destroyed shapes. */
    interface SensorEvents {
        begin: { sensor: Shape; visitor: Shape }[];
        end: { sensor: Shape | null; visitor: Shape | null }[];
    }

    /** A body that moved in the last step (awake bodies only; bodies destroyed since are left out). */
    interface BodyMoveEvent {
        body: Body;
        x: number;
        y: number;
        angle: number;
        /** The body went to sleep in this step. */
        fellAsleep: boolean;
    }

    /** Sizes of the simulation, from `World.getCounters()`. */
    interface Counters {
        bodyCount: number;
        shapeCount: number;
        contactCount: number;
        jointCount: number;
        islandCount: number;
        stackUsed: number;
        staticTreeHeight: number;
        treeHeight: number;
        taskCount: number;
        awakeContactCount: number;
        recycledContactCount: number;
        /** Bytes held by this world. */
        byteCount: number;
        /** Constraints per solver graph color (24 colors). */
        colorCounts: number[];
    }

    interface World {
        /**
         * Advances the simulation. Default 1/60 s and 4 sub-steps (1 to 64).
         * Use a fixed time step for stable results. Throws a RangeError, and
         * leaves the world as it was, once the memory budget is spent (see
         * `Box2D.setMemoryLimit()`).
         */
        step(timeStep?: number, subStepCount?: number): void;
        createBody(options?: BodyOptions): Body;

        createDistanceJoint(bodyA: Body, bodyB: Body, options?: DistanceJointOptions): Joint;
        createRevoluteJoint(bodyA: Body, bodyB: Body, options?: RevoluteJointOptions): Joint;
        createPrismaticJoint(bodyA: Body, bodyB: Body, options?: PrismaticJointOptions): Joint;
        createWeldJoint(bodyA: Body, bodyB: Body, options?: WeldJointOptions): Joint;
        createWheelJoint(bodyA: Body, bodyB: Body, options?: WheelJointOptions): Joint;
        createMotorJoint(bodyA: Body, bodyB: Body, options?: MotorJointOptions): Joint;
        /** Only disables collision between the two bodies. */
        createFilterJoint(bodyA: Body, bodyB: Body, options?: JointOptions): Joint;

        getGravity(): Vec2;
        setGravity(x: number, y: number): void;
        enableContinuous(flag: boolean): void;
        isContinuousEnabled(): boolean;
        enableSleeping(flag: boolean): void;
        isSleepingEnabled(): boolean;
        setRestitutionThreshold(value: number): void;
        getRestitutionThreshold(): number;
        setHitEventThreshold(value: number): void;
        getHitEventThreshold(): number;
        /** > 0. */
        setMaximumLinearSpeed(value: number): void;
        getMaximumLinearSpeed(): number;
        getAwakeBodyCount(): number;
        /**
         * Contact softness (advanced): stiffness in Hz, damping ratio and the
         * most speed (m/s) at which overlap is pushed out; all >= 0.
         * Defaults 30, 10, 3.
         */
        setContactTuning(hertz: number, dampingRatio: number, pushSpeed: number): void;
        /** Distance (m) within which contact points are reused between steps, >= 0 (0 disables). */
        setContactRecycleDistance(distance: number): void;
        getContactRecycleDistance(): number;
        /** Solver warm starting; disabling it only makes stacks less stable (testing). */
        enableWarmStarting(flag: boolean): void;
        isWarmStartingEnabled(): boolean;
        /** Speculative contacts (testing). */
        enableSpeculative(flag: boolean): void;
        getCounters(): Counters;
        /** Rebalances the tree of static shapes, e.g. after building a level. */
        rebuildStaticTree(): void;
        getUserData(): any;
        setUserData(value: any): void;

        /** Closest shape hit by the ray from origin to origin + translation, or null. */
        castRay(originX: number, originY: number, translationX: number, translationY: number,
            filter?: QueryFilter): CastHit | null;
        /** Every shape hit by the ray, nearest first. */
        raycastAll(originX: number, originY: number, translationX: number, translationY: number,
            filter?: QueryFilter): CastHit[];
        /** Shapes whose bounds overlap the box (a broad test: bounds are slightly padded). */
        queryAABB(lowerX: number, lowerY: number, upperX: number, upperY: number, filter?: QueryFilter): Shape[];
        /** Shapes overlapping the convex hull of 1 to 8 world points, rounded by radius. */
        overlapShape(points: Vec2[], radius?: number, filter?: QueryFilter): Shape[];
        overlapShape(points: Vec2[], filter?: QueryFilter): Shape[];
        overlapPolygon(vertices: Vec2[], radius?: number, filter?: QueryFilter): Shape[];
        overlapPolygon(vertices: Vec2[], filter?: QueryFilter): Shape[];
        overlapCircle(x: number, y: number, radius: number, filter?: QueryFilter): Shape[];
        overlapCapsule(x1: number, y1: number, x2: number, y2: number, radius: number, filter?: QueryFilter): Shape[];
        /** Sweeps a shape and returns every hit, nearest first. */
        castShape(points: Vec2[], radius: number, translationX: number, translationY: number,
            filter?: QueryFilter): CastHit[];
        castPolygon(vertices: Vec2[], radius: number, translationX: number, translationY: number,
            filter?: QueryFilter): CastHit[];
        castCircle(x: number, y: number, radius: number, translationX: number, translationY: number,
            filter?: QueryFilter): CastHit[];
        castCapsule(x1: number, y1: number, x2: number, y2: number, radius: number,
            translationX: number, translationY: number, filter?: QueryFilter): CastHit[];

        /**
         * Character mover: sweeps a capsule (centers x1,y1 / x2,y2 relative to
         * x,y; radius > 0.01) and returns the fraction of the translation
         * it can move.
         */
        castMover(x: number, y: number, x1: number, y1: number, x2: number, y2: number, radius: number,
            translationX: number, translationY: number, filter?: QueryFilter): number;
        /** Collision planes around a capsule mover, relative to x,y. */
        collideMover(x: number, y: number, x1: number, y1: number, x2: number, y2: number, radius: number,
            filter?: QueryFilter): MoverPlane[];

        /** Events of the last step. Only shapes with the matching enable*Events option report them. */
        getContactEvents(): ContactEvents;
        getSensorEvents(): SensorEvents;
        /** Cheapest way to sync sprites: only the bodies that moved. */
        getBodyEvents(): BodyMoveEvent[];
        /**
         * Joints whose force or torque exceeded their threshold in the last
         * step (breakable joints: destroy them here).
         */
        getJointEvents(): { joint: Joint }[];
        /**
         * Writes x, y, angle of each body into out[3i], out[3i + 1],
         * out[3i + 2] and returns the body count. No object is allocated:
         * the fastest way to sync sprites every frame.
         */
        readTransforms(bodies: Body[], out: Float32Array): number;
        /** Milliseconds spent in each phase of the last step (EE cycle counter). */
        getProfile(): {
            step: number; pairs: number; collide: number; solve: number; solverSetup: number;
            constraints: number; prepareConstraints: number; integrateVelocities: number; warmStart: number;
            solveImpulses: number; integratePositions: number; relaxImpulses: number; applyRestitution: number;
            storeImpulses: number; splitIslands: number; transforms: number; sensorHits: number;
            jointEvents: number; hitEvents: number; refit: number; bullets: number; sleepIslands: number;
            sensors: number;
        };
        /**
         * The whole simulation state, between steps. Checksummed: a damaged
         * image is rejected by `restore`. Valid for this build only.
         */
        snapshot(): ArrayBuffer;
        /**
         * Returns the world to a snapshot of itself. Objects that existed then
         * keep their script objects and user data; objects created since are
         * destroyed; objects destroyed since come back as new script objects.
         * RangeError for a rejected image (the world is unchanged). If the
         * image fails midway the world is destroyed and an InternalError thrown.
         */
        restore(image: ArrayBuffer | ArrayBufferView): true;
        /** Radial impulse on the shapes within radius + falloff. */
        explode(x: number, y: number, radius: number, falloff: number, impulsePerLength: number, maskBits?: Bits): void;

        /** Frees the world and everything in it. Returns false if already destroyed. */
        destroy(): boolean;
        isValid(): boolean;
    }

    interface Body {
        readonly world: World;

        getType(): BodyType;
        setType(type: BodyType): void;
        getPosition(): Vec2;
        /** Teleports the body, keeping its angle. */
        setPosition(x: number, y: number): void;
        /** Radians. */
        getAngle(): number;
        /** Position and angle in one call. */
        getTransform(): { x: number; y: number; angle: number };
        setTransform(x: number, y: number, angle: number): void;
        /** Moves a kinematic body to the target over `duration` seconds (> 0) with velocities. */
        setTargetTransform(x: number, y: number, angle: number, duration: number): void;
        getLinearVelocity(): Vec2;
        setLinearVelocity(vx: number, vy: number): void;
        getAngularVelocity(): number;
        setAngularVelocity(w: number): void;

        /** Force at a world point. `wake` defaults to true. */
        applyForce(fx: number, fy: number, px: number, py: number, wake?: boolean): void;
        applyForceToCenter(fx: number, fy: number, wake?: boolean): void;
        applyTorque(torque: number, wake?: boolean): void;
        applyLinearImpulse(ix: number, iy: number, px: number, py: number, wake?: boolean): void;
        applyLinearImpulseToCenter(ix: number, iy: number, wake?: boolean): void;
        applyAngularImpulse(impulse: number, wake?: boolean): void;

        getMass(): number;
        getLinearDamping(): number;
        setLinearDamping(damping: number): void;
        getAngularDamping(): number;
        setAngularDamping(damping: number): void;
        getGravityScale(): number;
        setGravityScale(scale: number): void;
        isAwake(): boolean;
        setAwake(flag: boolean): void;
        isEnabled(): boolean;
        setEnabled(flag: boolean): void;
        isFixedRotation(): boolean;
        setFixedRotation(flag: boolean): void;
        isBullet(): boolean;
        setBullet(flag: boolean): void;

        /** Local point to world. */
        getWorldPoint(x: number, y: number): Vec2;
        /** World point to local. */
        getLocalPoint(x: number, y: number): Vec2;
        /** Local direction to world (rotation only). */
        getWorldVector(x: number, y: number): Vec2;
        /** World direction to local (rotation only). */
        getLocalVector(x: number, y: number): Vec2;
        /** Velocity of a world point moving with the body. */
        getWorldPointVelocity(x: number, y: number): Vec2;
        /** Velocity of a local point of the body, in world coordinates. */
        getLocalPointVelocity(x: number, y: number): Vec2;
        /** Center of mass in world coordinates. */
        getWorldCenter(): Vec2;
        /** Center of mass in local coordinates. */
        getLocalCenter(): Vec2;
        /** About the center of mass, kg·m^2. */
        getRotationalInertia(): number;
        getMassData(): MassData;
        /**
         * Overrides the mass computed from the shapes (fields left out keep
         * their value; mass and inertia >= 0) until the shapes change or
         * `applyMassFromShapes()` is called.
         */
        setMassData(data: Partial<MassData>): void;
        /** Recomputes mass after shape density or geometry changes. */
        applyMassFromShapes(): void;
        computeAABB(): AABB;
        /** Drops the forces and torques applied since the last step. */
        clearForces(): void;
        /** Wakes the bodies touching this one. */
        wakeTouching(): void;
        /** When false the body never sleeps. Default true. */
        enableSleep(flag: boolean): void;
        isSleepEnabled(): boolean;
        /** Speed (m/s) below which the body may sleep, >= 0. */
        setSleepThreshold(threshold: number): void;
        getSleepThreshold(): number;
        /** See `BodyOptions.enableContactRecycling`. */
        enableContactRecycling(flag: boolean): void;
        isContactRecyclingEnabled(): boolean;
        /** Sets the flag on every shape of the body (read it with `shape.areContactEventsEnabled()`). */
        enableContactEvents(flag: boolean): void;
        /** Sets the flag on every shape of the body. */
        enableHitEvents(flag: boolean): void;
        /** Debug name, cut to 10 bytes at a character boundary. */
        setName(name: string): void;
        getName(): string;

        createCircleShape(options: CircleOptions): Shape;
        createBoxShape(options: BoxOptions): Shape;
        createPolygonShape(options: PolygonOptions): Shape;
        createCapsuleShape(options: CapsuleOptions): Shape;
        createSegmentShape(options: SegmentOptions): Shape;
        /**
         * Chain of segments from 4 or more local points, consecutive points
         * more than 0.005 apart. Segments are one-sided: they collide on the
         * right of the direction from one point to the next, so terrain
         * walked on from above is listed right to left, and a loop listed
         * counter-clockwise collides on the outside. An open chain uses its
         * first and last points only to smooth the ends: n points make
         * n - 3 segments (a loop makes n).
         */
        createChain(points: Vec2[], options?: ChainOptions): Chain;
        /** Shapes of the body, chain segments included. */
        getShapes(): Shape[];
        getJoints(): Joint[];
        /** Touching contacts of the body's shapes (e.g. "is the player grounded?"). */
        getContacts(): Contact[];
        getMotionLocks(): { linearX: boolean; linearY: boolean; angularZ: boolean };
        /** Fields left out keep their value; `angularZ` is `fixedRotation`. */
        setMotionLocks(locks: { linearX?: boolean; linearY?: boolean; angularZ?: boolean }): void;

        getUserData(): any;
        setUserData(value: any): void;
        /** Frees the body with its shapes, chains and joints. Returns false if already destroyed. */
        destroy(): boolean;
        isValid(): boolean;
    }

    interface Shape {
        readonly world: World;

        getType(): ShapeType;
        getBody(): Body;
        getFriction(): number;
        setFriction(friction: number): void;
        getRestitution(): number;
        setRestitution(restitution: number): void;
        getDensity(): number;
        /** `updateBodyMass` defaults to true. */
        setDensity(density: number, updateBodyMass?: boolean): void;
        isSensor(): boolean;
        enableSensorEvents(flag: boolean): void;
        areSensorEventsEnabled(): boolean;
        enableContactEvents(flag: boolean): void;
        areContactEventsEnabled(): boolean;
        enableHitEvents(flag: boolean): void;
        areHitEventsEnabled(): boolean;
        getFilter(): { categoryBits: Bits; maskBits: Bits; groupIndex: number };
        /** Fields left out keep their value. */
        setFilter(filter: Filter): void;
        getUserData(): any;
        setUserData(value: any): void;

        /** World point inside the shape. */
        testPoint(x: number, y: number): boolean;
        getClosestPoint(x: number, y: number): Vec2;
        /** World bounds, slightly padded. */
        getAABB(): AABB;
        /** Local geometry; each throws a TypeError for another shape type. */
        getCircle(): { center: Vec2; radius: number };
        getCapsule(): { center1: Vec2; center2: Vec2; radius: number };
        getPolygon(): { vertices: Vec2[]; normals: Vec2[]; centroid: Vec2; radius: number; count: number };
        getSegment(): { point1: Vec2; point2: Vec2 };
        getChainSegment(): { ghost1: Vec2; segment: { point1: Vec2; point2: Vec2 }; ghost2: Vec2; chain: Chain };
        /** Touching contacts of this shape. */
        getContacts(): Contact[];
        /** Sensors only: the shapes inside, as of the last step. */
        getSensorOverlaps(): Shape[];

        /**
         * Replace the geometry (the type may change), with the options of the
         * matching `create*Shape`. The body mass is kept: call
         * `body.applyMassFromShapes()`. Chain segments throw a TypeError.
         */
        setCircle(circle: { radius: number; center?: Vec2 }): void;
        setBox(box: { halfWidth: number; halfHeight: number; center?: Vec2; angle?: number }): void;
        setPolygon(polygon: { vertices: Vec2[]; radius?: number }): void;
        setCapsule(capsule: { point1: Vec2; point2: Vec2; radius: number }): void;
        setSegment(segment: { point1: Vec2; point2: Vec2 }): void;
        getSurfaceMaterial(): Required<SurfaceMaterial>;
        /** Fields left out keep their value. */
        setSurfaceMaterial(material: SurfaceMaterial): void;
        /** Mass of this shape alone, from its density. */
        computeMassData(): MassData;
        /** Ray against this shape only (world coordinates); null when missed. */
        rayCast(originX: number, originY: number, translationX: number, translationY: number): CastHit | null;
        /**
         * Air force on a circle, capsule or polygon of a dynamic body (other
         * shapes are ignored). `drag` (>= 0) scales the shape's own velocity
         * against the wind, `lift` the force across it. `wake` defaults to true.
         */
        applyWind(windX: number, windY: number, drag: number, lift: number, wake?: boolean): void;

        /**
         * `updateBodyMass` defaults to true. Returns false if already
         * destroyed. Chain segments go away with their chain: destroying one
         * throws a TypeError.
         */
        destroy(updateBodyMass?: boolean): boolean;
        isValid(): boolean;
    }

    interface Chain {
        readonly world: World;

        getBody(): Body;
        /** The chain segments (shapes of type 'chainSegment'). */
        getShapes(): Shape[];
        getUserData(): any;
        setUserData(value: any): void;
        destroy(): boolean;
        isValid(): boolean;
    }

    /**
     * Methods specific to a joint type throw a TypeError on other types.
     * Types noted per method.
     */
    interface Joint {
        readonly world: World;

        getType(): JointType;
        getBodyA(): Body;
        getBodyB(): Body;
        getUserData(): any;
        setUserData(value: any): void;
        setCollideConnected(flag: boolean): void;
        getCollideConnected(): boolean;
        wakeBodies(): void;
        /** See `JointOptions.forceThreshold` and `World.getJointEvents()`. */
        setForceThreshold(force: number): void;
        getForceThreshold(): number;
        setTorqueThreshold(torque: number): void;
        getTorqueThreshold(): number;
        getConstraintForce(): Vec2;
        getConstraintTorque(): number;
        /** Constraint error: meters apart the anchors are (ignoring allowed motion). */
        getLinearSeparation(): number;
        /** Constraint error in radians. */
        getAngularSeparation(): number;
        /** See `JointOptions.constraintHertz`; both >= 0. */
        setConstraintTuning(hertz: number, dampingRatio: number): void;
        getConstraintTuning(): { hertz: number; dampingRatio: number };
        /** Joint frame in body A's (or B's) local space: anchor and axis angle. */
        getLocalFrameA(): { x: number; y: number; angle: number };
        getLocalFrameB(): { x: number; y: number; angle: number };
        setLocalFrameA(x: number, y: number, angle: number): void;
        setLocalFrameB(x: number, y: number, angle: number): void;

        /** distance, revolute, prismatic, wheel. */
        enableSpring(flag: boolean): void;
        isSpringEnabled(): boolean;
        setSpringHertz(hertz: number): void;
        getSpringHertz(): number;
        setSpringDampingRatio(ratio: number): void;
        getSpringDampingRatio(): number;
        enableLimit(flag: boolean): void;
        isLimitEnabled(): boolean;
        /** Length range (distance), angle (revolute, within +-0.99 * PI) or translation (prismatic, wheel). */
        setLimits(lower: number, upper: number): void;
        getLimits(): { lower: number; upper: number };
        enableMotor(flag: boolean): void;
        isMotorEnabled(): boolean;
        setMotorSpeed(speed: number): void;
        getMotorSpeed(): number;

        /** distance, prismatic. */
        setMaxMotorForce(force: number): void;
        getMaxMotorForce(): number;
        getMotorForce(): number;
        /** revolute, wheel. */
        setMaxMotorTorque(torque: number): void;
        getMaxMotorTorque(): number;
        getMotorTorque(): number;

        /** revolute: current angle in radians. */
        getAngle(): number;
        /**
         * revolute: angle the spring pulls towards. Like the other setters,
         * this does not wake sleeping bodies: call `wakeBodies()`.
         */
        setTargetAngle(angle: number): void;
        getTargetAngle(): number;
        /** prismatic: translation along the axis and its speed. */
        getTranslation(): number;
        getSpeed(): number;
        /** prismatic: translation the spring pulls towards (does not wake the bodies). */
        setTargetTranslation(translation: number): void;
        getTargetTranslation(): number;
        /** distance: rest length (> 0) and current length. */
        getLength(): number;
        setLength(length: number): void;
        getCurrentLength(): number;
        /** distance: see `DistanceJointOptions.lowerSpringForce`; lower <= upper. */
        setSpringForceRange(lower: number, upper: number): void;
        getSpringForceRange(): { lower: number; upper: number };

        /** weld, motor. */
        setLinearHertz(hertz: number): void;
        getLinearHertz(): number;
        setLinearDampingRatio(ratio: number): void;
        getLinearDampingRatio(): number;
        setAngularHertz(hertz: number): void;
        getAngularHertz(): number;
        setAngularDampingRatio(ratio: number): void;
        getAngularDampingRatio(): number;

        /** motor. */
        setLinearVelocity(x: number, y: number): void;
        getLinearVelocity(): Vec2;
        setAngularVelocity(velocity: number): void;
        getAngularVelocity(): number;
        setMaxVelocityForce(force: number): void;
        getMaxVelocityForce(): number;
        setMaxVelocityTorque(torque: number): void;
        getMaxVelocityTorque(): number;
        setMaxSpringForce(force: number): void;
        getMaxSpringForce(): number;
        setMaxSpringTorque(torque: number): void;
        getMaxSpringTorque(): number;

        /** `wakeBodies` defaults to true. Returns false if already destroyed. */
        destroy(wakeBodies?: boolean): boolean;
        isValid(): boolean;
    }

    /**
     * Creates a world. At most `MAX_WORLDS` exist at a time: destroy() the
     * ones you no longer need (unreachable ones are collected first).
     */
    function createWorld(options?: WorldOptions): World;
}
