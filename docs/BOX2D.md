# Box2D physics

The `box2d` module is 2D rigid body physics with a vendored
[Box2D 3.2](https://box2d.org): bodies, five shape types and chains, seven
joint types, ray/shape casts, overlap queries, a character mover, contact,
sensor, body and joint events, and world snapshots. `box2ddraw` draws a world
for debugging. The script API is in `src/modules/box2d/box2d.d.ts` and
`src/modules/box2ddraw/box2ddraw.d.ts`.

Neither is in the default build:

```bash
node tools/modules.js configure --modules=box2d,box2ddraw,gamepad,font,screen
make
```

`bin/tests/box2d_example.js` is a playground (player, crates, joints,
snapshots) and `bin/tests/box2d_bench.js` measures the step.

## Getting started

```js
const world = Box2D.createWorld({ gravity: { x: 0, y: -10 } });

const ground = world.createBody({ position: { x: 0, y: -1 } });
ground.createBoxShape({ halfWidth: 20, halfHeight: 1 });

const balls = [];
for (let i = 0; i < 50; i++) {
    const ball = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: i % 10 - 5, y: 5 + i / 10 } });
    ball.createCircleShape({ radius: 0.4, restitution: 0.6 });
    balls.push(ball);
}

const transforms = new Float32Array(balls.length * 3);
while (true) {
    world.step(1 / 60, 4);
    world.readTransforms(balls, transforms);   // x, y, angle per body, no allocation
    for (let i = 0; i < balls.length; i++)
        drawBall(320 + transforms[3 * i] * 32, 400 - transforms[3 * i + 1] * 32, transforms[3 * i + 2]);
    Box2DDraw.draw(world, { scale: 32, offsetX: 320, offsetY: 400 });  // while debugging
    Screen.flip();
}
```

- **Units** are meters, kilograms and seconds. Box2D is tuned for moving
  objects of 0.1 to 10 m: simulate in meters and multiply by a
  pixels-per-meter factor when drawing (Box2D's y axis points up). Keep mass
  ratios between touching bodies near 10:1; a 1000:1 stack does not hold.
- **Step with a fixed time step** (1/60 s with 4 sub-steps is a good
  default). The EE has one core: every world runs on one worker.
- **Reading state costs allocations**: each `getPosition()` or
  `getTransform()` creates an object. For many bodies per frame use
  `world.readTransforms(bodies, float32Array)` (about 30 times faster for
  100 bodies on the build machine; `bin/tests/box2d_bench.js` measures it on
  the console) or `world.getBodyEvents()` (only the bodies that moved).
- **Measure** with `world.getProfile()`: milliseconds per phase of the last
  step, from the EE cycle counter.

## Objects and lifetime

Every Box2D object has exactly one script object, so `shape.getBody() ===
body`, and shapes from queries and events compare with `===` and work as
`Map` keys. Each object holds its world (`body.world`): a world stays alive
while any of its objects is reachable, and an unreachable world is freed by
the GC with everything in it.

`destroy()` frees at once: a body takes its shapes, chains and joints with
it, a world takes everything. Later calls on a destroyed object throw a
TypeError; `isValid()` returns false. At most `Box2D.MAX_WORLDS` (8) worlds
exist at a time — `createWorld()` runs the GC before giving up with a
RangeError, but destroy worlds you are done with (level changes).

Box2D worlds are not thread-safe, but the bindings never release the script
lock (GIL) during a call, so worlds may be used from `Thread` workers: each
call is atomic with respect to the other threads.

## Events and queries

Shapes report events only when enabled on them:

| Event | Option | Read with |
| --- | --- | --- |
| Contact begin/end | shape `enableContactEvents` | `world.getContactEvents().begin/.end` |
| Hit (impact speed above `hitEventThreshold`) | shape `enableHitEvents` | `world.getContactEvents().hit` |
| Sensor overlap | sensor: `isSensor` + `enableSensorEvents`; visitor: `enableSensorEvents` | `world.getSensorEvents()` |
| Joint force/torque over a threshold | joint `forceThreshold` / `torqueThreshold` | `world.getJointEvents()` |

Events describe the last `step()`. A shape destroyed since then reads as
`null` (always possible in end events, which also report destroyed shapes);
`getBodyEvents()` and `getJointEvents()` leave out objects destroyed since.

State queries, without events: `body.getContacts()` / `shape.getContacts()`
(touching contacts with their points and normal — e.g. "is the player on the
ground?") and `sensor.getSensorOverlaps()`.

## Character mover

A kinematic character moves with three calls per frame:

```js
const planes = world.collideMover(x, y, 0, -0.5, 0, 0.5, 0.3);   // capsule around (x, y)
const move = Box2D.solvePlanes(velocity.x * dt, velocity.y * dt, planes);
x += move.x; y += move.y;
velocity = Box2D.clipVector(velocity.x, velocity.y, planes);    // same planes: uses their push
```

`world.castMover()` returns how far a capsule can travel before hitting
something (for fast moves).

## Snapshots

`world.snapshot()` returns the whole simulation as an `ArrayBuffer`;
`world.restore(image)` brings the world back to it (rollback, retry, quick
saves). Objects that existed at the snapshot keep their script objects and
user data; objects created since are destroyed; objects destroyed since come
back with new script objects. Images carry a checksum: a damaged one (e.g. a
corrupted save file) throws a RangeError and leaves the world untouched. An
image only fits the build that wrote it.

## Validation and limits

Box2D checks its input with assertions that stop the console. The bindings
check everything first: a wrong type or object throws a TypeError, a value
out of range throws a RangeError — NaN, Infinity, negative sizes, a polygon
without area, revolute limits beyond ±0.99π, a 3-point chain, and:

- every number must be within ±1e10;
- positions, points, distances and `{ x, y }` vectors within ±100 000 m
  (Box2D's `B2_HUGE`; its precision degrades long before that).

The EE FPU has no infinity or NaN: a diverging simulation saturates at
±3.4e38 instead of producing NaN, and `isfinite()` is always true. Inputs
are therefore checked on their bits, and these limits keep values far from
saturation. The same rules are available to C code as `athena_box2d_*` in
`<athena/box2d.h>`.

**Memory.** Box2D cannot recover from a failed allocation. Creating a world,
body, shape, chain or joint throws a RangeError when Box2D would exceed
`Box2D.setMemoryLimit(bytes)` (no limit by default) or leave less than
256 KB of free RAM; `Box2D.getMemoryUsage()` reports what it holds. If an
allocation still fails, the console stops on the crash screen with the size
requested instead of crashing at random.

**Assertions** are compiled in debug builds (`make debug`) and stop on the
crash screen with the failed condition and its source line; release builds
leave them out, which the input checks make safe.

The world table is static: 8 slots of ~1.9 KB (~15.5 KB of BSS; upstream's
128 slots would take ~242 KB).

## Debug drawing

`Box2DDraw.draw(world, options)` draws shapes (static green, awake dynamic
pink, sleeping gray, kinematic blue, sensors wheat), joints and optionally
bounds, contacts and centers of mass. Lines are batched into one GS packet
stream per call and Box2D skips what is off screen. Options: `scale`
(pixels per meter), `offsetX`/`offsetY` (screen position of the world
origin, default the screen center), `flipY`, `fill`, `shapes`, `joints`,
`jointExtras`, `bounds`, `contacts`, `mass`.

## Differences from the old API (`old/`)

Every method and option of the old bindings exists with the same name.
Intentional differences:

- **Joint frames.** `anchor` and `axis` are world-space and both bodies'
  frames are computed from them, so a joint starts relaxed. The old
  bindings rotated only body A's frame by `axis` (a prismatic or wheel
  joint with a non-default axis twisted body B on the first step) and
  measured revolute angles from the bodies' absolute angles. With unrotated
  bodies the results are the same as before.
- **Distance joint default length** is the distance between the anchors
  (the old code used the body origins even when anchors were given), and
  never 0.
- **Chains need 4 points.** Box2D 3 uses the first and last points of an open
  chain as ghost vertices (n points make n − 3 segments); the old binding
  accepted 2 points and triggered a Box2D assertion. Segments are
  one-sided: list terrain right to left so it collides from above.
- **`collideMover`** returns the planes Box2D reports as hits, with the
  contact `point`. The old callback had the wrong signature for Box2D 3.2
  and read invalid planes.
- **`raycastAll` and the `cast*` methods** return hits sorted nearest first
  (the old order was arbitrary).
- **Stricter arguments:** numbers must be finite and within the limits
  above, extra arguments throw, flags accept booleans or numbers.
  `workerCount` above 1 is accepted but ignored (it used to hang the step).
- **Relaxed ranges** where Box2D allows them: `restitution` above 1,
  negative `gravityScale`, negative `impulsePerLength` in `explode`.
  `maximumLinearSpeed` must be above 0 (0 was accepted and asserted).
- **Collision bits** accept BigInt and read back exactly: a number within
  ±2^53 (two's complement, so the default mask is `-1`), a BigInt beyond.
- **Values are float32**, as in Box2D, and the angles set by a script read
  back exactly (Box2D's `b2MakeRot` approximation is avoided).
- **Additions:** `world.getUserData/setUserData`, `enableSleeping`,
  `isSleepingEnabled`, `get/setHitEventThreshold`, `getAwakeBodyCount`,
  `readTransforms`, `getProfile`, `getJointEvents`, `snapshot`, `restore`;
  `body.getTransform`, `isBullet/setBullet`, `getContacts`,
  `get/setMotionLocks`, `lockLinearX/Y` options, an optional `wake` flag on
  forces and impulses; `shape.getContacts`, `getSensorOverlaps`; joint
  `force/torqueThreshold`; `userData` in body/shape/chain options; chain
  materials and filters in `createChain`; `chain.getUserData/setUserData`;
  the `world` property on every object; `Box2D.solvePlanes`, `clipVector`,
  `setMemoryLimit`, `getMemoryLimit`, `getMemoryUsage`,
  `MAX_POLYGON_VERTICES`, `MAX_WORLDS`. `Box2D.version` reports the real
  library version.

## C API

With `RUNTIME=native`, include `<box2d/box2d.h>` and use Box2D directly
(<https://box2d.org/documentation/>). `<athena/box2d.h>` adds what the
bindings use: validity rules (`athena_box2d_valid_float` works on the EE),
`athena_box2d_joint_frames()`, a parameter dispatch over joint types, the
memory budget and checksummed snapshots. `<athena/box2ddraw.h>` draws a
world. Worlds must run with `workerCount = 1` (the default).

## Tests

- `tests/host/box2d_test.c` (`docker compose run --rm host-tests`): the
  native helpers against the real Box2D simulation, with UBSan.
- `bin/tests/box2d_test.js`, on the console or PCSX2, and on the build
  machine with `docker compose run --rm js-tests`: the script API under
  AddressSanitizer and UBSan, in a 32-bit userland as the QuickJS build
  requires. The EE-specific float behavior only shows on the console or
  PCSX2.
