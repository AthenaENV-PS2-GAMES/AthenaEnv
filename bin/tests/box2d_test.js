/*
 * Box2D module smoke and contract tests.
 *
 * Run with bin/ as the working directory (default_script=tests/box2d_test.js
 * in athena.ini). No files or hardware are needed: every check runs a small
 * simulation in memory. Build with the module selected, e.g.
 *   node tools/modules.js configure --modules=box2d,system
 */

let passed = 0;
let failed = 0;

function pass(name) {
    console.log("[PASS] " + name);
    passed++;
}

function fail(name, error) {
    console.log("[FAIL] " + name + ": " + error);
    failed++;
}

function test(name, callback) {
    try {
        callback();
        pass(name);
    } catch (error) {
        fail(name, error);
    }
}

function captureError(callback) {
    try {
        callback();
    } catch (error) {
        return error;
    }
    throw new Error("expected an exception");
}

function expectThrow(name, callback, type) {
    test(name, function() {
        const error = captureError(callback);
        if (type && !(error instanceof type))
            throw new Error("expected " + type.name + ", got " + error);
    });
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
}

function assertEqual(actual, expected, what) {
    if (actual !== expected)
        throw new Error(what + ": expected " + String(expected) + ", got " + String(actual));
}

function assertNear(actual, expected, tolerance, what) {
    if (typeof actual !== "number" || Math.abs(actual - expected) > tolerance)
        throw new Error(what + ": expected " + expected + " +- " + tolerance + ", got " + actual);
}

function step(world, count) {
    for (let i = 0; i < count; i++) world.step(1 / 60, 4);
}

/* A world with a static ground box whose top is at y = 0. */
function groundWorld(options) {
    const world = Box2D.createWorld(options || { gravity: { x: 0, y: -10 } });
    const ground = world.createBody({ position: { x: 0, y: -1 } });
    ground.createBoxShape({ halfWidth: 50, halfHeight: 1 });
    return { world: world, ground: ground };
}

function dynamicBox(world, x, y, options) {
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: x, y: y } });
    body.createBoxShape(Object.assign({ halfWidth: 0.5, halfHeight: 0.5 }, options || {}));
    return body;
}

/* ------------------------------------------------------------------------ */
/* Module                                                                    */
/* ------------------------------------------------------------------------ */

test("module exports", function() {
    assertEqual(typeof Box2D.createWorld, "function", "createWorld");
    assertEqual(Box2D.version, "3.2.0", "version");
    assertEqual(Box2D.STATIC_BODY, 0, "STATIC_BODY");
    assertEqual(Box2D.KINEMATIC_BODY, 1, "KINEMATIC_BODY");
    assertEqual(Box2D.DYNAMIC_BODY, 2, "DYNAMIC_BODY");
    assertEqual(Box2D.MAX_POLYGON_VERTICES, 8, "MAX_POLYGON_VERTICES");
    assert(Box2D.MAX_WORLDS >= 1, "MAX_WORLDS");
});

test("float results compare with number literals", function() {
    /* Box2D values are float32; mixed comparisons used to read their bits as an int. */
    const world = Box2D.createWorld();
    const y = world.createBody({ position: { x: 0, y: 0.49 } }).getPosition().y;
    assert(y > 0.1 && y < 0.9 && y <= 0.5 && y >= 0.4 && !(y < 0.3) && !(y > 0.7), "comparisons of " + y);
    assert(-y < 0 && -y > -1, "negative comparisons of " + (-y));
    world.destroy();
});

/* ------------------------------------------------------------------------ */
/* World                                                                     */
/* ------------------------------------------------------------------------ */

test("createWorld applies its options", function() {
    const userData = { level: 3 };
    const world = Box2D.createWorld({
        gravity: { x: 1, y: -5 },
        enableSleep: false,
        enableContinuous: false,
        restitutionThreshold: 2,
        hitEventThreshold: 3,
        maximumLinearSpeed: 50,
        workerCount: 1,
        userData: userData,
    });
    const gravity = world.getGravity();
    assertEqual(gravity.x, 1, "gravity.x");
    assertEqual(gravity.y, -5, "gravity.y");
    assertEqual(world.isSleepingEnabled(), false, "sleep");
    assertEqual(world.isContinuousEnabled(), false, "continuous");
    assertEqual(world.getRestitutionThreshold(), 2, "restitutionThreshold");
    assertEqual(world.getHitEventThreshold(), 3, "hitEventThreshold");
    assertEqual(world.getMaximumLinearSpeed(), 50, "maximumLinearSpeed");
    assertEqual(world.getUserData(), userData, "userData");
    assert(world.destroy(), "destroy");
});

test("world settings round-trip", function() {
    const world = Box2D.createWorld();
    world.setGravity(0, -20);
    assertEqual(world.getGravity().y, -20, "gravity");
    world.enableContinuous(true);
    assertEqual(world.isContinuousEnabled(), true, "continuous");
    world.enableSleeping(true);
    assertEqual(world.isSleepingEnabled(), true, "sleeping");
    world.setRestitutionThreshold(0.5);
    assertEqual(world.getRestitutionThreshold(), 0.5, "restitutionThreshold");
    world.setHitEventThreshold(1.5);
    assertEqual(world.getHitEventThreshold(), 1.5, "hitEventThreshold");
    world.setMaximumLinearSpeed(10);
    assertEqual(world.getMaximumLinearSpeed(), 10, "maximumLinearSpeed");
    world.setUserData("state");
    assertEqual(world.getUserData(), "state", "userData");
    assertEqual(world.getAwakeBodyCount(), 0, "awake bodies");
    world.destroy();
});

test("destroy invalidates the world and its objects", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY });
    const shape = body.createCircleShape({ radius: 1 });
    assert(world.isValid(), "valid before");
    assertEqual(world.destroy(), true, "first destroy");
    assertEqual(world.destroy(), false, "second destroy");
    assertEqual(world.isValid(), false, "world invalid");
    assertEqual(body.isValid(), false, "body invalid");
    assertEqual(shape.isValid(), false, "shape invalid");
    const error = captureError(function() { body.getPosition(); });
    assert(error instanceof TypeError && /destroyed/.test(error.message), "destroyed body error: " + error);
});

expectThrow("createWorld rejects non-object options", function() { Box2D.createWorld(5); }, TypeError);
expectThrow("createWorld rejects extra arguments", function() { Box2D.createWorld({}, 1); }, TypeError);
expectThrow("createWorld rejects a non-positive maximumLinearSpeed", function() {
    Box2D.createWorld({ maximumLinearSpeed: 0 });
}, RangeError);
expectThrow("createWorld rejects bad gravity", function() {
    Box2D.createWorld({ gravity: { x: "a", y: 0 } });
}, TypeError);
expectThrow("createWorld rejects infinite gravity", function() {
    Box2D.createWorld({ gravity: { x: 0, y: Infinity } });
}, RangeError);
expectThrow("step rejects a negative time step", function() { Box2D.createWorld().step(-1); }, RangeError);
expectThrow("step rejects zero sub-steps", function() { Box2D.createWorld().step(1 / 60, 0); }, RangeError);
expectThrow("world methods reject another this", function() {
    const world = Box2D.createWorld();
    world.step.call({});
}, TypeError);

test("unreachable worlds are collected and free their slots", function() {
    for (let i = 0; i < Box2D.MAX_WORLDS * 3; i++) {
        const world = Box2D.createWorld();
        const body = world.createBody({ type: Box2D.DYNAMIC_BODY, userData: { world: world } });
        body.createCircleShape({ radius: 0.5, userData: body });
        world.step();
    }
    std.gc();
});

test("too many live worlds throw RangeError", function() {
    const worlds = [];
    try {
        const error = captureError(function() {
            for (let i = 0; i <= Box2D.MAX_WORLDS; i++) worlds.push(Box2D.createWorld());
        });
        assert(error instanceof RangeError, "expected RangeError, got " + error);
    } finally {
        worlds.forEach(function(world) { world.destroy(); });
    }
});

/* ------------------------------------------------------------------------ */
/* Bodies                                                                    */
/* ------------------------------------------------------------------------ */

test("a dynamic body falls onto the ground and sleeps", function() {
    const scene = groundWorld();
    const box = dynamicBox(scene.world, 0, 4);
    step(scene.world, 240);
    assertNear(box.getPosition().y, 0.5, 0.05, "resting height");
    assertEqual(box.isAwake(), false, "asleep");
    scene.world.destroy();
});

test("createBody applies its options", function() {
    const world = Box2D.createWorld();
    const tag = { name: "player" };
    const body = world.createBody({
        type: Box2D.KINEMATIC_BODY,
        position: { x: 1, y: 2 },
        angle: 0.5,
        linearVelocity: { x: 3, y: 4 },
        angularVelocity: 0.25,
        linearDamping: 0.5,
        angularDamping: 0.75,
        gravityScale: 2,
        fixedRotation: true,
        isBullet: true,
        userData: tag,
    });
    assertEqual(body.getType(), Box2D.KINEMATIC_BODY, "type");
    assertEqual(body.getPosition().x, 1, "x");
    assertEqual(body.getPosition().y, 2, "y");
    assertNear(body.getAngle(), 0.5, 1e-3, "angle");
    assertEqual(body.getLinearVelocity().y, 4, "linearVelocity");
    assertEqual(body.getAngularVelocity(), 0.25, "angularVelocity");
    assertEqual(body.getLinearDamping(), 0.5, "linearDamping");
    assertEqual(body.getAngularDamping(), 0.75, "angularDamping");
    assertEqual(body.getGravityScale(), 2, "gravityScale");
    assertEqual(body.isFixedRotation(), true, "fixedRotation");
    assertEqual(body.isBullet(), true, "isBullet");
    assertEqual(body.getUserData(), tag, "userData");
    assertEqual(body.world, world, "world property");
    const rotated = world.createBody({ rotation: 1 });
    assertNear(rotated.getAngle(), 1, 1e-3, "rotation alias");
    world.destroy();
});

test("body state round-trips", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: 0 } });
    const body = dynamicBox(world, 0, 0);
    body.setPosition(3, 4);
    assertEqual(body.getPosition().x, 3, "setPosition");
    body.setTransform(-1, 2, 0.25);
    const transform = body.getTransform();
    assertEqual(transform.x, -1, "transform.x");
    assertEqual(transform.y, 2, "transform.y");
    assertNear(transform.angle, 0.25, 1e-3, "transform.angle");
    body.setLinearVelocity(1, -1);
    assertEqual(body.getLinearVelocity().x, 1, "linearVelocity");
    body.setAngularVelocity(-2);
    assertEqual(body.getAngularVelocity(), -2, "angularVelocity");
    body.setLinearDamping(0.1);
    assertNear(body.getLinearDamping(), 0.1, 1e-6, "linearDamping");
    body.setAngularDamping(0.2);
    assertNear(body.getAngularDamping(), 0.2, 1e-6, "angularDamping");
    body.setGravityScale(-1);
    assertEqual(body.getGravityScale(), -1, "negative gravityScale");
    body.setAwake(false);
    assertEqual(body.isAwake(), false, "awake");
    body.setEnabled(false);
    assertEqual(body.isEnabled(), false, "disabled");
    body.setEnabled(true);
    body.setFixedRotation(true);
    assertEqual(body.isFixedRotation(), true, "fixedRotation");
    body.setBullet(true);
    assertEqual(body.isBullet(), true, "bullet");
    body.setType(Box2D.STATIC_BODY);
    assertEqual(body.getType(), Box2D.STATIC_BODY, "setType");
    world.destroy();
});

test("forces and impulses move the body", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: 0 } });
    const body = dynamicBox(world, 0, 0);
    assertNear(body.getMass(), 1, 1e-4, "mass of a 1x1 box with density 1");
    body.applyLinearImpulseToCenter(2, 0);
    assertNear(body.getLinearVelocity().x, 2, 1e-4, "impulse to center");
    body.applyLinearImpulse(0, 1, 0, 0);
    assertNear(body.getLinearVelocity().y, 1, 1e-4, "impulse at the center point");
    body.applyAngularImpulse(0.5);
    assert(body.getAngularVelocity() > 0, "angular impulse");
    body.applyForceToCenter(60, 0);
    body.applyForce(0, 60, 0, 0, true);
    body.applyTorque(1, false);
    world.step(1 / 60, 4);
    assert(body.getLinearVelocity().x > 2, "force to center");
    assert(body.getLinearVelocity().y > 1, "force at point");
    world.destroy();
});

test("body transforms and bounds", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 10, y: 0 }, angle: Math.PI / 2 });
    body.createBoxShape({ halfWidth: 1, halfHeight: 0.5 });
    const p = body.getWorldPoint(1, 0);
    assertNear(p.x, 10, 1e-4, "world point x");
    assertNear(p.y, 1, 1e-4, "world point y");
    const local = body.getLocalPoint(10, 1);
    assertNear(local.x, 1, 1e-4, "local point x");
    assertNear(body.getWorldCenter().x, 10, 1e-4, "world center");
    const aabb = body.computeAABB();
    /* Box2D pads shape bounds by a small margin. */
    assertNear(aabb.upperY - aabb.lowerY, 2, 0.05, "rotated AABB height");
    body.applyMassFromShapes();
    world.destroy();
});

test("setTargetTransform drives a kinematic body", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.KINEMATIC_BODY });
    body.setTargetTransform(1, 0, 0, 1 / 60);
    world.step(1 / 60, 4);
    assertNear(body.getPosition().x, 1, 1e-3, "reached target");
    world.destroy();
});

test("body destroy cascades to shapes and joints", function() {
    const world = Box2D.createWorld();
    const a = dynamicBox(world, 0, 0);
    const b = dynamicBox(world, 2, 0);
    const shape = a.getShapes()[0];
    const joint = world.createRevoluteJoint(a, b, { anchor: { x: 1, y: 0 } });
    assertEqual(a.getJoints()[0], joint, "joint listed on the body");
    assertEqual(a.destroy(), true, "destroy");
    assertEqual(a.destroy(), false, "second destroy");
    assertEqual(shape.isValid(), false, "shape destroyed");
    assertEqual(joint.isValid(), false, "joint destroyed");
    assertEqual(b.getJoints().length, 0, "other body lost the joint");
    world.destroy();
});

expectThrow("createBody rejects an unknown type", function() {
    Box2D.createWorld().createBody({ type: 3 });
}, RangeError);
expectThrow("createBody rejects negative damping", function() {
    Box2D.createWorld().createBody({ linearDamping: -1 });
}, RangeError);
expectThrow("setType rejects an unknown type", function() {
    Box2D.createWorld().createBody().setType(7);
}, RangeError);
expectThrow("setPosition rejects missing arguments", function() {
    Box2D.createWorld().createBody().setPosition(1);
}, TypeError);
expectThrow("setPosition rejects NaN", function() {
    Box2D.createWorld().createBody().setPosition(NaN, 0);
}, RangeError);
expectThrow("setTargetTransform rejects a zero duration", function() {
    Box2D.createWorld().createBody({ type: Box2D.KINEMATIC_BODY }).setTargetTransform(0, 0, 0, 0);
}, RangeError);
expectThrow("body methods reject objects of another class", function() {
    const world = Box2D.createWorld();
    const shape = world.createBody().createCircleShape({ radius: 1 });
    world.createBody().getPosition.call(shape);
}, TypeError);

/* ------------------------------------------------------------------------ */
/* Shapes                                                                    */
/* ------------------------------------------------------------------------ */

test("shape constructors and geometry", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY });
    const circle = body.createCircleShape({ radius: 0.5, center: { x: 1, y: 0 } });
    assertEqual(circle.getType(), "circle", "circle type");
    assertEqual(circle.getCircle().radius, 0.5, "circle radius");
    assertEqual(circle.getCircle().center.x, 1, "circle center");

    const box = body.createBoxShape({ halfWidth: 1, halfHeight: 2, center: { x: 0, y: 1 }, angle: 0 });
    const polygon = box.getPolygon();
    assertEqual(box.getType(), "polygon", "box type");
    assertEqual(polygon.count, 4, "box vertices");
    assertEqual(polygon.vertices.length, 4, "vertices array");
    assertNear(polygon.centroid.y, 1, 1e-5, "box centroid");

    const triangle = body.createPolygonShape({
        vertices: [{ x: -1, y: 0 }, { x: 1, y: 0 }, { x: 0, y: 1 }],
        radius: 0.05,
    });
    assertEqual(triangle.getPolygon().count, 3, "triangle");
    assertNear(triangle.getPolygon().radius, 0.05, 1e-6, "rounded polygon");

    const capsule = body.createCapsuleShape({ point1: { x: 0, y: 0 }, point2: { x: 0, y: 1 }, radius: 0.25 });
    assertEqual(capsule.getType(), "capsule", "capsule type");
    assertEqual(capsule.getCapsule().center2.y, 1, "capsule center2");

    const segment = world.createBody().createSegmentShape({ point1: { x: -1, y: 0 }, point2: { x: 1, y: 0 } });
    assertEqual(segment.getType(), "segment", "segment type");
    assertEqual(segment.getSegment().point2.x, 1, "segment point2");

    assertEqual(body.getShapes().length, 4, "body shapes");
    assertEqual(circle.getBody(), body, "getBody identity");
    assertEqual(circle.world, world, "shape world property");
    world.destroy();
});

test("shape properties round-trip", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY });
    const shape = body.createCircleShape({
        radius: 1, density: 2, friction: 0.3, restitution: 1.5, rollingResistance: 0.1,
        tangentSpeed: 1, enableContactEvents: true, enableHitEvents: true, enableSensorEvents: true,
        filter: { categoryBits: 2, maskBits: 0xFFFF, groupIndex: -3 },
        userData: "wheel",
    });
    assertEqual(shape.getDensity(), 2, "density");
    assertNear(shape.getFriction(), 0.3, 1e-6, "friction");
    assertEqual(shape.getRestitution(), 1.5, "restitution above 1");
    assertEqual(shape.areContactEventsEnabled(), true, "contact events");
    assertEqual(shape.areHitEventsEnabled(), true, "hit events");
    assertEqual(shape.areSensorEventsEnabled(), true, "sensor events");
    assertEqual(shape.getUserData(), "wheel", "userData");
    assertEqual(shape.getFilter().categoryBits, 2, "categoryBits");
    assertEqual(shape.getFilter().maskBits, 0xFFFF, "maskBits");
    assertEqual(shape.getFilter().groupIndex, -3, "groupIndex");

    shape.setFriction(0.9);
    assertNear(shape.getFriction(), 0.9, 1e-6, "setFriction");
    shape.setRestitution(0);
    assertEqual(shape.getRestitution(), 0, "setRestitution");
    shape.setDensity(4, false);
    assertEqual(shape.getDensity(), 4, "setDensity");
    body.applyMassFromShapes();
    assertNear(body.getMass(), 4 * Math.PI, 1e-3, "mass from density");
    shape.enableContactEvents(false);
    shape.enableHitEvents(false);
    shape.enableSensorEvents(false);
    assertEqual(shape.areContactEventsEnabled(), false, "contact events off");
    shape.setFilter({ groupIndex: 5 });
    assertEqual(shape.getFilter().categoryBits, 2, "setFilter keeps omitted fields");
    assertEqual(shape.getFilter().groupIndex, 5, "setFilter groupIndex");
    shape.setUserData(null);
    assertEqual(shape.getUserData(), null, "setUserData");
    world.destroy();
});

test("64-bit collision masks", function() {
    const world = Box2D.createWorld();
    const shape = world.createBody().createCircleShape({ radius: 1 });
    assertEqual(shape.getFilter().maskBits, -1, "default mask reads as -1");
    shape.setFilter({ categoryBits: 2 ** 40, maskBits: -2 });
    assertEqual(shape.getFilter().categoryBits, 2 ** 40, "bit 40 as a number");
    assertEqual(shape.getFilter().maskBits, -2, "two's complement mask");
    shape.setFilter({ categoryBits: 1n << 60n, maskBits: 0xFFFFFFFF });
    assertEqual(shape.getFilter().categoryBits, 1n << 60n, "beyond 2^53 as a BigInt");
    assertEqual(shape.getFilter().maskBits, 0xFFFFFFFF, "32-bit mask");
    world.destroy();
});

test("sensor shapes", function() {
    const world = Box2D.createWorld();
    const sensor = world.createBody().createCircleShape({ radius: 1, isSensor: true });
    assertEqual(sensor.isSensor(), true, "isSensor");
    world.destroy();
});

test("point queries on a shape", function() {
    const world = Box2D.createWorld();
    const shape = world.createBody().createBoxShape({ halfWidth: 1, halfHeight: 1 });
    assertEqual(shape.testPoint(0.5, 0.5), true, "inside");
    assertEqual(shape.testPoint(2, 0), false, "outside");
    const closest = shape.getClosestPoint(3, 0);
    assertNear(closest.x, 1, 1e-4, "closest point");
    const aabb = shape.getAABB();
    assertNear(aabb.lowerX, -1, 0.05, "AABB lowerX");
    assertNear(aabb.upperY, 1, 0.05, "AABB upperY");
    world.destroy();
});

test("shape destroy", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY });
    const a = body.createCircleShape({ radius: 1 });
    body.createCircleShape({ radius: 1, center: { x: 1, y: 0 } });
    const mass = body.getMass();
    assertEqual(a.destroy(), true, "destroy");
    assertEqual(a.destroy(), false, "second destroy");
    assert(body.getMass() < mass, "mass updated");
    assertEqual(body.getShapes().length, 1, "one shape left");
    world.destroy();
});

expectThrow("createCircleShape requires a radius", function() {
    Box2D.createWorld().createBody().createCircleShape({});
}, TypeError);
expectThrow("createCircleShape rejects a zero radius", function() {
    Box2D.createWorld().createBody().createCircleShape({ radius: 0 });
}, RangeError);
expectThrow("createBoxShape rejects negative sizes", function() {
    Box2D.createWorld().createBody().createBoxShape({ halfWidth: -1, halfHeight: 1 });
}, RangeError);
expectThrow("createPolygonShape rejects collinear vertices", function() {
    Box2D.createWorld().createBody().createPolygonShape({ vertices: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 2, y: 0 }] });
}, RangeError);
expectThrow("createPolygonShape rejects more than 8 vertices", function() {
    const vertices = [];
    for (let i = 0; i < 9; i++) vertices.push({ x: Math.cos(i), y: Math.sin(i) });
    Box2D.createWorld().createBody().createPolygonShape({ vertices: vertices });
}, RangeError);
expectThrow("createPolygonShape rejects bad vertices", function() {
    Box2D.createWorld().createBody().createPolygonShape({ vertices: [{ x: 0 }, { x: 1, y: 0 }, { x: 0, y: 1 }] });
}, TypeError);
expectThrow("createSegmentShape rejects a zero-length segment", function() {
    Box2D.createWorld().createBody().createSegmentShape({ point1: { x: 0, y: 0 }, point2: { x: 0, y: 0 } });
}, RangeError);
expectThrow("createCapsuleShape rejects coincident points", function() {
    Box2D.createWorld().createBody().createCapsuleShape({ point1: { x: 0, y: 0 }, point2: { x: 0, y: 0 }, radius: 1 });
}, RangeError);
expectThrow("shape options reject negative friction", function() {
    Box2D.createWorld().createBody().createCircleShape({ radius: 1, friction: -1 });
}, RangeError);
expectThrow("getCircle rejects other shape types", function() {
    Box2D.createWorld().createBody().createBoxShape({ halfWidth: 1, halfHeight: 1 }).getCircle();
}, TypeError);
expectThrow("setFilter rejects fractional bits", function() {
    Box2D.createWorld().createBody().createCircleShape({ radius: 1 }).setFilter({ categoryBits: 1.5 });
}, RangeError);

/* ------------------------------------------------------------------------ */
/* Chains                                                                    */
/* ------------------------------------------------------------------------ */

test("chains", function() {
    const scene = groundWorld();
    const terrain = scene.world.createBody();
    const chain = terrain.createChain([
        /* One-sided: listed right to left so the solid side faces up. */
        { x: 20, y: 5 }, { x: 10, y: 2 }, { x: 0, y: 2 }, { x: -10, y: 2 }, { x: -20, y: 5 },
    ], { friction: 0.2, filter: { categoryBits: 4 }, userData: "terrain" });
    const segments = chain.getShapes();
    assertEqual(segments.length, 2, "open chain: n - 3 segments");
    assertEqual(segments[0].getType(), "chainSegment", "segment type");
    assertEqual(segments[0].getFilter().categoryBits, 4, "chain filter");
    assertNear(segments[0].getFriction(), 0.2, 1e-6, "chain friction");
    const info = segments[0].getChainSegment();
    assertEqual(info.chain, chain, "segment parent chain identity");
    assertEqual(info.ghost1.x, 20, "ghost1");
    assertEqual(chain.getBody(), terrain, "chain body");
    assertEqual(chain.getUserData(), "terrain", "chain userData");
    chain.setUserData(7);
    assertEqual(chain.getUserData(), 7, "chain setUserData");

    const box = dynamicBox(scene.world, 0, 6);
    step(scene.world, 180);
    assertNear(box.getPosition().y, 2.5, 0.05, "box rests on the chain");

    const error = captureError(function() { segments[0].destroy(); });
    assert(error instanceof TypeError, "chain segments are destroyed with the chain: " + error);
    assertEqual(chain.destroy(), true, "chain destroy");
    assertEqual(chain.isValid(), false, "chain invalid");
    assertEqual(segments[0].isValid(), false, "segments invalid");
    scene.world.destroy();
});

test("loop chains and body destroy", function() {
    const world = Box2D.createWorld();
    const body = world.createBody();
    const chain = body.createChain([{ x: 0, y: 0 }, { x: 4, y: 0 }, { x: 4, y: 4 }, { x: 0, y: 4 }], { isLoop: true });
    assertEqual(chain.getShapes().length, 4, "loop: n segments");
    body.destroy();
    assertEqual(chain.isValid(), false, "chain destroyed with its body");
    world.destroy();
});

expectThrow("createChain needs 4 points", function() {
    Box2D.createWorld().createBody().createChain([{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 2, y: 0 }]);
}, RangeError);
expectThrow("createChain rejects repeated points", function() {
    Box2D.createWorld().createBody().createChain([{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 0 }, { x: 2, y: 0 }]);
}, RangeError);
expectThrow("createChain rejects non-arrays", function() {
    Box2D.createWorld().createBody().createChain({});
}, TypeError);

/* ------------------------------------------------------------------------ */
/* Joints                                                                    */
/* ------------------------------------------------------------------------ */

function jointPair(gravity) {
    const world = Box2D.createWorld({ gravity: gravity || { x: 0, y: 0 } });
    return { world: world, a: dynamicBox(world, 0, 0), b: dynamicBox(world, 2, 0) };
}

test("distance joint", function() {
    const p = jointPair();
    const joint = p.world.createDistanceJoint(p.a, p.b, {
        enableSpring: true, hertz: 2, dampingRatio: 0.5, enableLimit: true, minLength: 1, maxLength: 3,
        enableMotor: true, maxMotorForce: 10, motorSpeed: 1, collideConnected: true, userData: "rope",
    });
    assertEqual(joint.getType(), "distance", "type");
    assertNear(joint.getLength(), 2, 1e-4, "default length is the current distance");
    assertNear(joint.getCurrentLength(), 2, 1e-4, "current length");
    assertEqual(joint.isSpringEnabled(), true, "spring");
    assertEqual(joint.getSpringHertz(), 2, "hertz");
    assertEqual(joint.getSpringDampingRatio(), 0.5, "damping");
    assertEqual(joint.getLimits().lower, 1, "minLength");
    assertEqual(joint.getLimits().upper, 3, "maxLength");
    assertEqual(joint.getMaxMotorForce(), 10, "maxMotorForce");
    assertEqual(joint.getCollideConnected(), true, "collideConnected");
    assertEqual(joint.getUserData(), "rope", "userData");
    assertEqual(joint.getBodyA(), p.a, "bodyA");
    assertEqual(joint.getBodyB(), p.b, "bodyB");
    joint.setLength(1.5);
    assertEqual(joint.getLength(), 1.5, "setLength");
    joint.setLimits(0.5, 4);
    assertEqual(joint.getLimits().upper, 4, "setLimits");
    joint.setMaxMotorForce(5);
    joint.setMotorSpeed(-1);
    assertEqual(joint.getMotorSpeed(), -1, "motorSpeed");
    assertEqual(typeof joint.getMotorForce(), "number", "motorForce");
    step(p.world, 10);
    p.world.destroy();
});

test("distance joint with a shared world anchor", function() {
    const p = jointPair();
    const joint = p.world.createDistanceJoint(p.a, p.b, { anchor: { x: 1, y: 0 } });
    assert(joint.getLength() > 0, "coincident anchors keep a positive length");
    const local = p.world.createDistanceJoint(p.a, p.b, { anchorA: { x: 0.5, y: 0 }, anchorB: { x: -0.5, y: 0 } });
    assertNear(local.getLength(), 1, 1e-4, "local anchors");
    p.world.destroy();
});

test("revolute joint starts relaxed between rotated bodies", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: 0 } });
    const a = world.createBody({ type: Box2D.DYNAMIC_BODY, angle: 0.7 });
    const b = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 2, y: 0 }, angle: -0.4 });
    a.createCircleShape({ radius: 0.5 });
    b.createCircleShape({ radius: 0.5 });
    const joint = world.createRevoluteJoint(a, b, {
        anchor: { x: 1, y: 0 }, enableLimit: true, lowerAngle: -0.5, upperAngle: 0.5,
        enableMotor: true, motorSpeed: 1, maxMotorTorque: 2, enableSpring: false,
    });
    assertNear(joint.getAngle(), 0, 1e-3, "zero angle at creation");
    assertEqual(joint.isLimitEnabled(), true, "limit");
    assertEqual(joint.getLimits().lower, -0.5, "lowerAngle");
    assertEqual(joint.getMaxMotorTorque(), 2, "maxMotorTorque");
    step(world, 30);
    assert(joint.getAngle() > 0.1 && joint.getAngle() <= 0.55, "motor turns up to the limit: " + joint.getAngle());
    assertEqual(typeof joint.getMotorTorque(), "number", "motor torque");
    joint.enableMotor(false);
    assertEqual(joint.isMotorEnabled(), false, "motor off");
    world.destroy();
});

test("prismatic joint moves along a world axis", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });
    const ground = world.createBody({ angle: 0.3 });
    ground.createBoxShape({ halfWidth: 0.5, halfHeight: 0.5 });
    const slider = dynamicBox(world, 0, 3);
    const joint = world.createPrismaticJoint(ground, slider, {
        anchor: { x: 0, y: 3 }, axis: { x: 0, y: 1 }, enableLimit: true,
        lowerTranslation: -2, upperTranslation: 2, enableMotor: false, maxMotorForce: 5,
    });
    assertNear(joint.getTranslation(), 0, 1e-4, "starts at zero");
    step(world, 120);
    assertNear(joint.getTranslation(), -2, 0.05, "falls to the lower limit");
    assertNear(slider.getPosition().x, 0, 1e-3, "no sideways motion");
    assertNear(slider.getAngle(), 0, 1e-3, "no rotation");
    assertEqual(typeof joint.getSpeed(), "number", "speed");
    world.destroy();
});

test("weld joint keeps the relative angle", function() {
    const p = jointPair();
    p.b.setTransform(2, 0, 0.5);
    const joint = p.world.createWeldJoint(p.a, p.b, {
        anchor: { x: 1, y: 0 }, linearHertz: 0, angularHertz: 0, linearDampingRatio: 1, angularDampingRatio: 1,
    });
    p.b.setAngularVelocity(2);
    step(p.world, 60);
    assertNear(p.b.getAngle() - p.a.getAngle(), 0.5, 0.05, "relative angle");
    joint.setLinearHertz(5);
    assertEqual(joint.getLinearHertz(), 5, "linearHertz");
    joint.setAngularDampingRatio(0.3);
    assertNear(joint.getAngularDampingRatio(), 0.3, 1e-6, "angularDampingRatio");
    p.world.destroy();
});

test("wheel joint", function() {
    const p = jointPair();
    const joint = p.world.createWheelJoint(p.a, p.b, {
        anchor: { x: 2, y: 0 }, enableSpring: true, hertz: 4, dampingRatio: 0.7,
        enableMotor: true, motorSpeed: 3, maxMotorTorque: 10,
    });
    assertEqual(joint.getType(), "wheel", "type");
    assertEqual(joint.getSpringHertz(), 4, "hertz");
    assertEqual(joint.getMotorSpeed(), 3, "motorSpeed");
    joint.setLimits(-1, 1);
    joint.enableLimit(true);
    assertEqual(joint.getLimits().upper, 1, "limits");
    step(p.world, 10);
    p.world.destroy();
});

test("motor joint", function() {
    const p = jointPair();
    const joint = p.world.createMotorJoint(p.a, p.b, {
        linearVelocity: { x: 1, y: 0 }, angularVelocity: 0.5, maxVelocityForce: 100, maxVelocityTorque: 50,
        linearHertz: 1, linearDampingRatio: 0.5, maxSpringForce: 10, angularHertz: 2, angularDampingRatio: 0.25,
        maxSpringTorque: 5,
    });
    assertEqual(joint.getLinearVelocity().x, 1, "linearVelocity");
    assertEqual(joint.getAngularVelocity(), 0.5, "angularVelocity");
    assertEqual(joint.getMaxVelocityForce(), 100, "maxVelocityForce");
    assertEqual(joint.getMaxVelocityTorque(), 50, "maxVelocityTorque");
    assertEqual(joint.getMaxSpringForce(), 10, "maxSpringForce");
    assertEqual(joint.getMaxSpringTorque(), 5, "maxSpringTorque");
    assertEqual(joint.getAngularHertz(), 2, "angularHertz");
    joint.setLinearVelocity(0, 2);
    assertEqual(joint.getLinearVelocity().y, 2, "setLinearVelocity");
    joint.setAngularVelocity(-1);
    joint.setMaxVelocityForce(1);
    joint.setMaxVelocityTorque(1);
    joint.setMaxSpringForce(1);
    joint.setMaxSpringTorque(1);
    joint.setLinearDampingRatio(1);
    joint.setAngularHertz(1);
    step(p.world, 10);
    p.world.destroy();
});

test("filter joint disables collision between its bodies", function() {
    const scene = groundWorld();
    const a = dynamicBox(scene.world, 0, 0.5);
    const b = dynamicBox(scene.world, 0, 0.9);
    const joint = scene.world.createFilterJoint(a, b);
    assertEqual(joint.getType(), "filter", "type");
    step(scene.world, 60);
    assert(b.getPosition().y < 0.9, "b sinks into a");
    scene.world.destroy();
});

test("common joint methods", function() {
    const p = jointPair();
    const joint = p.world.createRevoluteJoint(p.a, p.b);
    joint.setCollideConnected(true);
    assertEqual(joint.getCollideConnected(), true, "collideConnected");
    joint.wakeBodies();
    joint.setUserData({ id: 1 });
    assertEqual(joint.getUserData().id, 1, "userData");
    assertEqual(typeof joint.getConstraintForce().x, "number", "constraint force");
    assertEqual(typeof joint.getConstraintTorque(), "number", "constraint torque");
    assertEqual(joint.destroy(false), true, "destroy");
    assertEqual(joint.isValid(), false, "invalid");
    assertEqual(joint.destroy(), false, "second destroy");
    p.world.destroy();
});

expectThrow("joint methods reject other joint types", function() {
    const p = jointPair();
    p.world.createWeldJoint(p.a, p.b).getMotorSpeed();
}, TypeError);
expectThrow("setLimits rejects lower > upper", function() {
    const p = jointPair();
    p.world.createPrismaticJoint(p.a, p.b).setLimits(1, -1);
}, RangeError);
expectThrow("revolute limits beyond 0.99 PI throw", function() {
    const p = jointPair();
    p.world.createRevoluteJoint(p.a, p.b, { lowerAngle: -4, upperAngle: 0 });
}, RangeError);
expectThrow("joints need two different bodies", function() {
    const p = jointPair();
    p.world.createWeldJoint(p.a, p.a);
}, TypeError);
expectThrow("joints need bodies of the same world", function() {
    const p = jointPair();
    const q = jointPair();
    p.world.createWeldJoint(p.a, q.b);
}, TypeError);
expectThrow("joints reject non-bodies", function() {
    const p = jointPair();
    p.world.createWeldJoint(p.a, {});
}, TypeError);
expectThrow("prismatic joints reject a zero axis", function() {
    const p = jointPair();
    p.world.createPrismaticJoint(p.a, p.b, { axis: { x: 0, y: 0 } });
}, RangeError);
expectThrow("spring hertz must be >= 0", function() {
    const p = jointPair();
    p.world.createDistanceJoint(p.a, p.b).setSpringHertz(-1);
}, RangeError);

/* ------------------------------------------------------------------------ */
/* Queries                                                                   */
/* ------------------------------------------------------------------------ */

function queryScene() {
    const world = Box2D.createWorld();
    const left = world.createBody({ position: { x: -5, y: 0 } });
    const right = world.createBody({ position: { x: 5, y: 0 } });
    return {
        world: world,
        left: left.createBoxShape({ halfWidth: 1, halfHeight: 1, filter: { categoryBits: 1 } }),
        right: right.createBoxShape({ halfWidth: 1, halfHeight: 1, filter: { categoryBits: 2 } }),
    };
}

test("castRay returns the closest hit", function() {
    const s = queryScene();
    const hit = s.world.castRay(-10, 0, 20, 0);
    assertEqual(hit.shape, s.left, "closest shape");
    assertNear(hit.point.x, -6, 1e-3, "hit point");
    assertNear(hit.normal.x, -1, 1e-3, "hit normal");
    assertNear(hit.fraction, 0.2, 1e-3, "fraction");
    assertEqual(s.world.castRay(-10, 5, 20, 0), null, "miss");
    assertEqual(s.world.castRay(-10, 0, 20, 0, { maskBits: 2 }).shape, s.right, "filtered");
    s.world.destroy();
});

test("raycastAll returns every hit, nearest first", function() {
    const s = queryScene();
    const hits = s.world.raycastAll(10, 0, -20, 0);
    assertEqual(hits.length, 2, "two hits");
    assertEqual(hits[0].shape, s.right, "nearest first");
    assert(hits[0].fraction < hits[1].fraction, "sorted");
    s.world.destroy();
});

test("overlap queries", function() {
    const s = queryScene();
    assertEqual(s.world.queryAABB(-7, -1, -4, 1).length, 1, "queryAABB");
    assertEqual(s.world.queryAABB(-10, -10, 10, 10, { maskBits: 2 })[0], s.right, "queryAABB filter");
    assertEqual(s.world.overlapCircle(-5, 0, 0.5)[0], s.left, "overlapCircle");
    assertEqual(s.world.overlapCircle(0, 0, 0.5).length, 0, "overlapCircle miss");
    assertEqual(s.world.overlapCapsule(-6, 0, 6, 0, 0.1).length, 2, "overlapCapsule");
    assertEqual(s.world.overlapShape([{ x: 5, y: 0 }], 0.2)[0], s.right, "overlapShape with radius");
    assertEqual(s.world.overlapShape([{ x: 5, y: 0 }], { maskBits: 1 }).length, 0, "overlapShape with filter");
    assertEqual(s.world.overlapShape([{ x: 5, y: 0 }], undefined, { maskBits: 2 }).length, 1,
        "overlapShape with undefined radius and filter");
    assertEqual(s.world.overlapPolygon([{ x: 4, y: -1 }, { x: 6, y: -1 }, { x: 5, y: 1 }]).length, 1, "overlapPolygon");
    s.world.destroy();
});

test("shape casts", function() {
    const s = queryScene();
    const circle = s.world.castCircle(-10, 0, 0.5, 20, 0);
    assertEqual(circle.length, 2, "castCircle hits both");
    assertEqual(circle[0].shape, s.left, "nearest first");
    assertEqual(s.world.castCapsule(-10, -0.5, -10, 0.5, 0.25, 20, 0, { maskBits: 2 })[0].shape, s.right,
        "castCapsule with filter");
    assertEqual(s.world.castShape([{ x: -10, y: 0 }], 0.1, 20, 0).length, 2, "castShape");
    assertEqual(s.world.castPolygon([{ x: -10, y: -0.2 }, { x: -9.8, y: -0.2 }, { x: -9.9, y: 0.2 }], 0, 20, 0).length, 2,
        "castPolygon");
    s.world.destroy();
});

test("character mover", function() {
    const scene = groundWorld({ gravity: { x: 0, y: 0 } });
    const fraction = scene.world.castMover(0, 3, 0, -0.5, 0, 0.5, 0.3, 0, -5);
    assert(fraction > 0 && fraction < 1, "castMover stops at the ground: " + fraction);
    const planes = scene.world.collideMover(0, 0.7, 0, -0.5, 0, 0.5, 0.3);
    assert(planes.length >= 1, "collideMover finds the ground");
    assertEqual(planes[0].shape.getBody(), scene.ground, "plane shape");
    assertNear(planes[0].normal.y, 1, 1e-3, "plane normal");
    assertEqual(typeof planes[0].offset, "number", "plane offset");
    assertEqual(typeof planes[0].point.x, "number", "plane point");
    scene.world.destroy();
});

expectThrow("queryAABB rejects inverted bounds", function() { queryScene().world.queryAABB(1, 1, 0, 0); }, RangeError);
expectThrow("castRay rejects missing arguments", function() { queryScene().world.castRay(0, 0, 1); }, TypeError);
expectThrow("castRay rejects a bad filter", function() { queryScene().world.castRay(0, 0, 1, 0, 5); }, TypeError);
expectThrow("overlapPolygon needs 3 vertices", function() {
    queryScene().world.overlapPolygon([{ x: 0, y: 0 }, { x: 1, y: 0 }]);
}, RangeError);
expectThrow("castMover rejects a tiny radius", function() {
    queryScene().world.castMover(0, 0, 0, 0, 0, 1, 0.001, 1, 0);
}, RangeError);

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

test("contact begin and hit events", function() {
    const scene = groundWorld();
    const ball = scene.world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 0, y: 3 } });
    const shape = ball.createCircleShape({ radius: 0.5, enableContactEvents: true, enableHitEvents: true });
    scene.ground.getShapes()[0].enableContactEvents(true);
    scene.ground.getShapes()[0].enableHitEvents(true);
    let begins = 0, hits = 0;
    for (let i = 0; i < 90; i++) {
        scene.world.step(1 / 60, 4);
        const events = scene.world.getContactEvents();
        events.begin.forEach(function(e) {
            if (e.shapeA === shape || e.shapeB === shape) begins++;
        });
        events.hit.forEach(function(e) {
            assert(e.approachSpeed > 0, "approach speed");
            assertEqual(typeof e.point.x, "number", "hit point");
            hits++;
        });
    }
    assertEqual(begins, 1, "one begin event");
    assert(hits >= 1, "hit events: " + hits);

    ball.destroy();
    scene.world.step(1 / 60, 4);
    const end = scene.world.getContactEvents().end;
    assertEqual(end.length, 1, "end event for the destroyed ball");
    assert(end[0].shapeA === null || end[0].shapeB === null, "destroyed shapes are null");
    scene.world.destroy();
});

test("sensor events", function() {
    const scene = groundWorld();
    const zone = scene.world.createBody({ position: { x: 0, y: 2 } });
    const sensor = zone.createBoxShape({ halfWidth: 2, halfHeight: 0.5, isSensor: true, enableSensorEvents: true });
    const ball = scene.world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 0, y: 5 } });
    const visitor = ball.createCircleShape({ radius: 0.25, enableSensorEvents: true });
    let began = false, ended = false;
    for (let i = 0; i < 120; i++) {
        scene.world.step(1 / 60, 4);
        const events = scene.world.getSensorEvents();
        events.begin.forEach(function(e) {
            if (e.sensor === sensor && e.visitor === visitor) began = true;
        });
        events.end.forEach(function(e) {
            if (e.sensor === sensor && e.visitor === visitor) ended = true;
        });
    }
    assert(began, "begin event");
    assert(ended, "end event");
    scene.world.destroy();
});

test("body move events", function() {
    const scene = groundWorld();
    const box = dynamicBox(scene.world, 1, 3);
    let moves = 0, slept = false;
    for (let i = 0; i < 300; i++) {
        scene.world.step(1 / 60, 4);
        scene.world.getBodyEvents().forEach(function(e) {
            assertEqual(e.body, box, "event body identity");
            assertEqual(typeof e.angle, "number", "angle");
            assertNear(e.x, box.getPosition().x, 1e-4, "event position");
            if (e.fellAsleep) slept = true;
            moves++;
        });
    }
    assert(moves > 10, "move events: " + moves);
    assert(slept, "fellAsleep");
    scene.world.destroy();
});

test("events after destroying their objects", function() {
    const scene = groundWorld();
    const boxes = [];
    for (let i = 0; i < 4; i++) {
        const box = dynamicBox(scene.world, i * 2 - 3, 0.6, { enableContactEvents: true, enableSensorEvents: true });
        boxes.push(box);
    }
    scene.ground.getShapes()[0].enableContactEvents(true);
    scene.world.step(1 / 60, 4);
    /* Destroyed between the step and the read, then collected. */
    boxes[1].destroy();
    boxes[2].destroy();
    boxes[1] = boxes[2] = null;
    std.gc();
    const moves = scene.world.getBodyEvents();
    assertEqual(moves.length, 2, "destroyed bodies are skipped");
    moves.forEach(function(e) { assert(e.body.isValid(), "live bodies only"); });
    const contacts = scene.world.getContactEvents();
    contacts.begin.forEach(function(e) {
        assert(e.shapeA === null || e.shapeA.isValid(), "begin shapeA");
        assert(e.shapeB === null || e.shapeB.isValid(), "begin shapeB");
    });
    scene.world.destroy();
});

test("explode pushes bodies away", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: 0 } });
    const box = dynamicBox(world, 2, 0);
    world.explode(0, 0, 5, 1, 10);
    world.step(1 / 60, 4);
    assert(box.getLinearVelocity().x > 0, "pushed away");
    world.explode(0, 0, 5, 1, 10, 0);
    world.destroy();
});

expectThrow("explode rejects a negative radius", function() {
    Box2D.createWorld().explode(0, 0, -1, 1, 1);
}, RangeError);

/* ------------------------------------------------------------------------ */
/* Lifetime                                                                  */
/* ------------------------------------------------------------------------ */

test("objects keep their world alive", function() {
    let body = (function() {
        const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });
        return dynamicBox(world, 0, 10);
    })();
    std.gc();
    body.world.step(1 / 60, 4);
    assert(body.getPosition().y < 10, "world still simulates");
    body.world.destroy();
    body = null;
    std.gc();
});

test("wrappers are unique per Box2D object", function() {
    const world = Box2D.createWorld();
    const body = world.createBody();
    const shape = body.createCircleShape({ radius: 1 });
    assertEqual(body.getShapes()[0], shape, "same shape object");
    assertEqual(world.queryAABB(-1, -1, 1, 1)[0], shape, "same object from a query");
    const map = new Map();
    map.set(shape, "tagged");
    assertEqual(map.get(world.castRay(-5, 0, 10, 0).shape), "tagged", "usable as Map key");
    world.destroy();
});

test("many objects are released by destroy and the GC", function() {
    for (let round = 0; round < 3; round++) {
        const scene = groundWorld();
        for (let i = 0; i < 50; i++) {
            const body = dynamicBox(scene.world, (i % 10) - 5, 1 + Math.floor(i / 10), { userData: { i: i } });
            if (i % 7 === 0) body.destroy();
        }
        step(scene.world, 30);
        scene.world.raycastAll(-10, 0.5, 20, 0);
        scene.world.getContactEvents();
        if (round !== 1) scene.world.destroy();
    }
    std.gc();
});

/* ------------------------------------------------------------------------ */
/* Value ranges                                                              */
/* ------------------------------------------------------------------------ */

expectThrow("positions beyond B2_HUGE throw", function() {
    Box2D.createWorld().createBody({ position: { x: 2e5, y: 0 } });
}, RangeError);
expectThrow("setTransform beyond B2_HUGE throws", function() {
    Box2D.createWorld().createBody().setTransform(0, -1e6, 0);
}, RangeError);
expectThrow("forces beyond 1e10 throw", function() {
    Box2D.createWorld().createBody({ type: Box2D.DYNAMIC_BODY }).applyForceToCenter(1e11, 0);
}, RangeError);
expectThrow("chain points beyond B2_HUGE throw", function() {
    Box2D.createWorld().createBody().createChain([{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 2, y: 0 }, { x: 3e5, y: 0 }]);
}, RangeError);
test("the largest coordinates are accepted", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ position: { x: 1e5, y: -1e5 } });
    assertEqual(body.getPosition().x, 1e5, "x");
    body.applyForceToCenter(1e10, 0);
    world.destroy();
});

/* ------------------------------------------------------------------------ */
/* Memory budget                                                             */
/* ------------------------------------------------------------------------ */

test("memory usage and limit", function() {
    const world = Box2D.createWorld();
    const used = Box2D.getMemoryUsage();
    assert(used > 0, "a world uses memory: " + used);
    assertEqual(Box2D.getMemoryLimit(), 0, "no limit by default");
    Box2D.setMemoryLimit(used);
    try {
        const error = captureError(function() {
            for (let i = 0; i < 10000; i++) dynamicBox(world, i % 100, Math.floor(i / 100));
        });
        assert(error instanceof RangeError && /memory/.test(error.message), "limit reached: " + error);
        assert(Box2D.getMemoryUsage() < used * 4, "stopped near the limit");
    } finally {
        Box2D.setMemoryLimit(0);
    }
    world.createBody();
    world.destroy();
});
expectThrow("setMemoryLimit rejects negative sizes", function() { Box2D.setMemoryLimit(-1); }, RangeError);

/* ------------------------------------------------------------------------ */
/* Batch reads and profile                                                   */
/* ------------------------------------------------------------------------ */

test("readTransforms fills a Float32Array", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: 0 } });
    const bodies = [world.createBody({ position: { x: 1, y: 2 }, angle: 0.5 }), world.createBody({ position: { x: -3, y: 4 } })];
    const out = new Float32Array(8);
    out[6] = 99;
    assertEqual(world.readTransforms(bodies, out), 2, "count");
    assertEqual(out[0], 1, "x0");
    assertEqual(out[1], 2, "y0");
    assertNear(out[2], 0.5, 1e-3, "angle0");
    assertEqual(out[3], -3, "x1");
    assertEqual(out[6], 99, "past the bodies untouched");
    assertEqual(world.readTransforms([], out), 0, "empty");
    world.destroy();
});
expectThrow("readTransforms needs a Float32Array", function() {
    const world = Box2D.createWorld();
    world.readTransforms([world.createBody()], new Int32Array(3));
}, TypeError);
expectThrow("readTransforms needs room for every body", function() {
    const world = Box2D.createWorld();
    world.readTransforms([world.createBody(), world.createBody()], new Float32Array(5));
}, RangeError);
expectThrow("readTransforms rejects destroyed bodies", function() {
    const world = Box2D.createWorld();
    const body = world.createBody();
    body.destroy();
    world.readTransforms([body], new Float32Array(3));
}, TypeError);
expectThrow("readTransforms rejects bodies of another world", function() {
    const world = Box2D.createWorld();
    world.readTransforms([Box2D.createWorld().createBody()], new Float32Array(3));
}, TypeError);
test("readTransforms survives getters that detach the buffer", function() {
    const world = Box2D.createWorld();
    const body = world.createBody();
    const out = new Float32Array(3);
    const bodies = [];
    Object.defineProperty(bodies, 0, { get: function() { return body; }, enumerable: true });
    bodies.length = 1;
    world.readTransforms(bodies, out);
    world.destroy();
});

test("getProfile reports the step phases", function() {
    const scene = groundWorld();
    dynamicBox(scene.world, 0, 2);
    scene.world.step(1 / 60, 4);
    const profile = scene.world.getProfile();
    ["step", "collide", "solve", "solveImpulses", "sleepIslands"].forEach(function(name) {
        assert(typeof profile[name] === "number" && profile[name] >= 0, name + ": " + profile[name]);
    });
    scene.world.destroy();
});

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                 */
/* ------------------------------------------------------------------------ */

test("snapshot and restore", function() {
    const scene = groundWorld();
    const box = dynamicBox(scene.world, 0, 5);
    box.setUserData("crate");
    const gone = dynamicBox(scene.world, 3, 5);
    const goneShape = gone.getShapes()[0];
    const image = scene.world.snapshot();
    assert(image instanceof ArrayBuffer && image.byteLength > 0, "image");

    step(scene.world, 60);
    assert(box.getPosition().y < 4, "fell");
    gone.destroy();
    const late = dynamicBox(scene.world, -3, 5);

    assertEqual(scene.world.restore(image), true, "restore");
    assertEqual(box.isValid(), true, "existing body kept");
    assertEqual(box.getUserData(), "crate", "user data kept");
    assertNear(box.getPosition().y, 5, 1e-4, "position restored");
    assertEqual(late.isValid(), false, "body created after the snapshot is gone");
    assertEqual(goneShape.isValid(), false, "old script object stays destroyed");
    const back = scene.world.queryAABB(2.9, 4.9, 3.1, 5.1);
    assertEqual(back.length, 1, "destroyed body is back");
    assert(back[0] !== goneShape && back[0].isValid(), "with a new script object");
    step(scene.world, 60);
    assertEqual(scene.world.restore(new Uint8Array(image)), true, "restore from a view");
    assertNear(box.getPosition().y, 5, 1e-4, "restored again");
    scene.world.destroy();
});

test("damaged snapshots are rejected and the world is kept", function() {
    const scene = groundWorld();
    const box = dynamicBox(scene.world, 0, 5);
    const image = new Uint8Array(scene.world.snapshot());
    image[image.length >> 1] ^= 0xFF;
    const error = captureError(function() { scene.world.restore(image); });
    assert(error instanceof RangeError && /damaged/.test(error.message), "checksum mismatch: " + error);
    assertEqual(box.isValid(), true, "world unchanged");
    step(scene.world, 2);
    const other = Box2D.createWorld();
    assert(captureError(function() { other.restore(new ArrayBuffer(16)); }) instanceof RangeError, "garbage");
    assert(captureError(function() { other.restore({}); }) instanceof TypeError, "not bytes");
    scene.world.destroy();
});

/* ------------------------------------------------------------------------ */
/* Joint events, contacts, sensors, motion locks                             */
/* ------------------------------------------------------------------------ */

test("joint events past the force threshold", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });
    const anchor = world.createBody({ position: { x: 0, y: 5 } });
    const weight = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 0, y: 3 } });
    weight.createBoxShape({ halfWidth: 0.5, halfHeight: 0.5, density: 10 });
    const rope = world.createDistanceJoint(anchor, weight, { forceThreshold: 20 });
    assertEqual(rope.getForceThreshold(), 20, "threshold");
    let broke = 0;
    for (let i = 0; i < 30; i++) {
        world.step(1 / 60, 4);
        world.getJointEvents().forEach(function(e) {
            assertEqual(e.joint, rope, "event joint");
            if (e.joint.isValid()) { e.joint.destroy(); broke++; }
        });
    }
    assertEqual(broke, 1, "the 100 N weight breaks a 20 N rope");
    rope.isValid();
    const strong = world.createDistanceJoint(anchor, weight);
    strong.setForceThreshold(1e6);
    strong.setTorqueThreshold(1e6);
    assertEqual(strong.getTorqueThreshold(), 1e6, "torque threshold");
    world.destroy();
});

test("contacts of a resting body", function() {
    const scene = groundWorld();
    const box = dynamicBox(scene.world, 0, 0.6);
    step(scene.world, 30);
    const contacts = box.getContacts();
    assertEqual(contacts.length, 1, "one contact");
    const c = contacts[0];
    assert(c.shapeA.getBody() === box || c.shapeB.getBody() === box, "our shape");
    assertNear(Math.abs(c.normal.y), 1, 1e-3, "vertical normal");
    assertEqual(c.points.length, 2, "two points on a flat face");
    assertNear(c.points[0].point.y, 0, 0.05, "points on the ground");
    assert(c.points[0].normalImpulse > 0, "carrying weight");
    assertEqual(box.getShapes()[0].getContacts().length, 1, "shape contacts");
    assertEqual(dynamicBox(scene.world, 10, 10).getContacts().length, 0, "airborne");
    scene.world.destroy();
});

test("sensor overlaps", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: 0 } });
    const sensor = world.createBody().createCircleShape({ radius: 2, isSensor: true, enableSensorEvents: true });
    const inside = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 1, y: 0 } })
        .createCircleShape({ radius: 0.5, enableSensorEvents: true });
    world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 10, y: 0 } })
        .createCircleShape({ radius: 0.5, enableSensorEvents: true });
    world.step(1 / 60, 4);
    const overlaps = sensor.getSensorOverlaps();
    assertEqual(overlaps.length, 1, "one visitor");
    assertEqual(overlaps[0], inside, "the one inside");
    assert(captureError(function() { inside.getSensorOverlaps(); }) instanceof TypeError, "not a sensor");
    world.destroy();
});

test("motion locks", function() {
    const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });
    const slider = world.createBody({ type: Box2D.DYNAMIC_BODY, lockLinearX: true });
    slider.createCircleShape({ radius: 0.5 });
    slider.applyLinearImpulseToCenter(5, 0);
    step(world, 10);
    assertNear(slider.getPosition().x, 0, 1e-4, "x locked");
    assert(slider.getPosition().y < 0, "y free");
    const locks = slider.getMotionLocks();
    assertEqual(locks.linearX, true, "linearX");
    assertEqual(locks.linearY, false, "linearY");
    slider.setMotionLocks({ linearY: true });
    assertEqual(slider.getMotionLocks().linearX, true, "partial update keeps linearX");
    assertEqual(slider.isFixedRotation(), false, "angularZ untouched");
    slider.setMotionLocks({ angularZ: true });
    assertEqual(slider.isFixedRotation(), true, "angularZ is fixedRotation");
    world.destroy();
});

test("character mover with solvePlanes and clipVector", function() {
    const scene = groundWorld({ gravity: { x: 0, y: 0 } });
    /* Capsule standing just above the ground (top at y = 0). */
    let y = 0.85;
    for (let i = 0; i < 30; i++) {
        const planes = scene.world.collideMover(0, y, 0, -0.5, 0, 0.5, 0.3);
        const move = Box2D.solvePlanes(0, -0.2, planes);
        assertEqual(typeof move.iterations, "number", "iterations");
        y += move.y;
    }
    assert(y > 0.75 && y < 0.85, "rests on the ground at " + y);
    /* As in Box2D: solve, then clip the velocity with the same planes. */
    const planes = scene.world.collideMover(0, y, 0, -0.5, 0, 0.5, 0.3);
    Box2D.solvePlanes(0, -0.2, planes);
    assert(planes[0].push !== 0, "push written back: " + planes[0].push);
    const velocity = Box2D.clipVector(3, -5, planes);
    assertNear(velocity.y, 0, 1e-3, "downward velocity clipped");
    assertNear(velocity.x, 3, 1e-3, "sideways velocity kept");
    assertEqual(Box2D.solvePlanes(1, 0, []).x, 1, "no planes: free move");
    scene.world.destroy();
});
expectThrow("solvePlanes checks its planes", function() { Box2D.solvePlanes(0, 0, [{ offset: 1 }]); }, TypeError);

/* ------------------------------------------------------------------------ */
/* Arguments whose getters destroy what the call works on                    */
/* ------------------------------------------------------------------------ */

expectThrow("a shape option getter destroying the body", function() {
    const world = Box2D.createWorld();
    const body = world.createBody();
    body.createCircleShape({ radius: 1, get friction() { body.destroy(); return 0.5; } });
}, TypeError);
expectThrow("a joint option getter destroying a body", function() {
    const p = jointPair();
    p.world.createRevoluteJoint(p.a, p.b, { get anchor() { p.b.destroy(); return { x: 1, y: 0 }; } });
}, TypeError);
expectThrow("a late joint option getter destroying a body", function() {
    const p = jointPair();
    p.world.createWheelJoint(p.a, p.b, { get motorSpeed() { p.a.destroy(); return 1; } });
}, TypeError);
expectThrow("a distance joint length getter destroying a body", function() {
    const p = jointPair();
    p.world.createDistanceJoint(p.a, p.b, { get maxMotorForce() { p.a.destroy(); return 1; } });
}, TypeError);
expectThrow("a body option getter destroying the world", function() {
    const world = Box2D.createWorld();
    world.createBody({ get position() { world.destroy(); return { x: 0, y: 0 }; } });
}, TypeError);
expectThrow("a query filter getter destroying the world", function() {
    const world = Box2D.createWorld();
    world.castRay(0, 0, 1, 0, { get maskBits() { world.destroy(); return 1; } });
}, TypeError);
expectThrow("a filter getter destroying the shape", function() {
    const world = Box2D.createWorld();
    const shape = world.createBody().createCircleShape({ radius: 1 });
    shape.setFilter({ get groupIndex() { shape.destroy(); return 1; } });
}, TypeError);
expectThrow("a chain point getter destroying the body", function() {
    const world = Box2D.createWorld();
    const body = world.createBody();
    body.createChain([{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 2, y: 0 }, { get x() { body.destroy(); return 3; }, y: 0 }]);
}, TypeError);
expectThrow("an array getter destroying a body already read", function() {
    const world = Box2D.createWorld();
    const a = world.createBody(), b = world.createBody();
    const bodies = [a];
    Object.defineProperty(bodies, 1, { get: function() { a.destroy(); return b; }, enumerable: true });
    world.readTransforms(bodies, new Float32Array(6));
}, TypeError);
expectThrow("a motion lock getter destroying the body", function() {
    const world = Box2D.createWorld();
    const body = world.createBody({ type: Box2D.DYNAMIC_BODY });
    body.setMotionLocks({ get linearX() { body.destroy(); return true; } });
}, TypeError);

/* ------------------------------------------------------------------------ */
/* Stress: the simulation stays bounded (no NaN on the EE to catch divergence) */
/* ------------------------------------------------------------------------ */

function assertBounded(world, bodies, limit) {
    const out = new Float32Array(bodies.length * 3);
    world.readTransforms(bodies, out);
    for (let i = 0; i < out.length; i++)
        assert(out[i] === out[i] && Math.abs(out[i]) < limit, "value " + i + " = " + out[i]);
}

test("stress: a pyramid settles", function() {
    const scene = groundWorld();
    const boxes = [];
    for (let row = 0; row < 10; row++)
        for (let i = 0; i < 10 - row; i++)
            boxes.push(dynamicBox(scene.world, i - (10 - row) / 2 + 0.5, 0.5 + row * 1.001));
    step(scene.world, 300);
    assertBounded(scene.world, boxes, 50);
    assert(boxes[boxes.length - 1].getPosition().y > 8, "the top stays up: " + boxes[boxes.length - 1].getPosition().y);
    scene.world.destroy();
});

test("stress: huge explosions stay bounded by the speed cap", function() {
    const scene = groundWorld({ gravity: { x: 0, y: -10 }, maximumLinearSpeed: 100 });
    const boxes = [];
    for (let i = 0; i < 20; i++) boxes.push(dynamicBox(scene.world, (i % 5) - 2, 1 + Math.floor(i / 5)));
    scene.world.explode(0, 1, 5, 5, 1e9);
    scene.world.step(1 / 60, 4);
    /* The cap applies when velocities are integrated; contacts solved
     * afterwards add some (up to ~2.2x the cap measured on the EE, whose
     * rounding differs from x86). Without the cap a 1e9 impulse would give
     * speeds near 1e9: 10x the cap still catches a runaway. */
    boxes.forEach(function(box) {
        const v = box.getLinearVelocity();
        assert(Math.sqrt(v.x * v.x + v.y * v.y) <= 1000, "speed " + v.x + ", " + v.y);
    });
    step(scene.world, 60);
    assertBounded(scene.world, boxes, 1e4);
    scene.world.destroy();
});

test("stress: 1000:1 mass ratio settles without exploding", function() {
    /* Box2D does not keep such a stack (the light box is squeezed out; keep
     * ratios near 10:1), but both boxes must come to rest on the ground. */
    const scene = groundWorld();
    const light = dynamicBox(scene.world, 0, 0.5, { density: 1 });
    const heavy = dynamicBox(scene.world, 0, 1.5, { density: 1000 });
    step(scene.world, 300);
    assertBounded(scene.world, [light, heavy], 10);
    assert(light.getPosition().y > 0.3 && heavy.getPosition().y > 0.3, "nothing through the ground");
    assertEqual(light.isAwake() || heavy.isAwake(), false, "at rest");
    scene.world.destroy();
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Box2D tests failed");
