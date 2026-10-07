import {Matrix4} from "Matrix4";
import * as Scene3D from "Scene3D";
import * as Collision3D from "Collision3D";
import * as Physics3D from "Physics3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function near(a, b, e = 1e-2) { return Math.abs(a - b) < e; }
const level = new Collision3D.World();
level.addBox([-20, -1, -20], [20, 0, 20]);
const world = new Physics3D.World(level, {iterations: 8});
const ball = world.addSphere({radius: .5, position: [0, 3, 0]});
const crate = world.addBox({halfExtents: [.5, .5, .5], mass: 2, position: [3, 2, 0]});
assert(world.bodyCount === 2 && ball.type === "dynamic" && ball.alive, "bodies");
for (let i = 0; i < 240; i++) world.step(1 / 60);
assert(near(ball.y, .5) && near(crate.y, .5) && ball.sleeping && crate.sleeping, "rest and sleep");
const q = crate.getRotation();
assert(q.length === 4 && near(Math.abs(q[3]), 1, 1e-3), "upright crate");
// Impulses wake bodies; nodes follow position and rotation.
const node = new Scene3D.Node();
crate.bind(node).applyImpulse(4, 0, 0);
assert(!crate.sleeping && crate.vx > 1, "impulse");
world.step(1 / 60);
assert(near(node.getLocalTransform(new Matrix4()).get(12), crate.x, 1e-4), "bound node");
crate.vy = 3; assert(near(crate.vy, 3, 1e-6), "velocity setter");
// Kinematic and static bodies.
const wall = world.addBox({type: "static", halfExtents: [.5, 2, 5], position: [10, 2, 0]});
const lift = world.addBox({type: "kinematic", halfExtents: [1, .1, 1], position: [-5, .5, 0], velocity: [0, 1, 0]});
assert(wall.type === "static" && lift.type === "kinematic", "types");
world.step(1 / 60); assert(lift.y > .5, "kinematic moves");
throws(() => world.addSphere({radius: 0}), RangeError, "invalid radius");
throws(() => world.addBox({type: "floating"}), RangeError, "invalid type");
throws(() => world.addSphere({restitution: 2}), RangeError, "invalid restitution");
throws(() => new Physics3D.World({}), TypeError, "statics must be a Collision3D.World");
throws(() => crate.applyImpulse(1, 2), TypeError, "impulse arity");
// Removal and disposal.
ball.remove(); assert(!ball.alive && world.bodyCount === 3, "remove");
assert(ball.setPosition(0, 1, 0) === ball && near(ball.y, 1, 1e-6), "removed bodies keep their state");
assert(world.attachLoop() === world && world.attached && world.detachLoop() && !world.attached, "loop");
crate.dispose(); throws(() => crate.x, TypeError, "disposed body");
world.dispose(); level.dispose(); node.dispose();
// Capsules: upright one rests on its cap, a lying one on its side.
const pills = new Physics3D.World(level2());
function level2() { const l = new Collision3D.World(); l.addBox([-20, -1, -20], [20, 0, 20]); return l; }
const upright = pills.addCapsule({radius: .3, halfHeight: .5, position: [0, 3, 0]});
const s = Math.SQRT1_2;
const lying = pills.addCapsule({radius: .3, halfHeight: .5, position: [3, 2, 0], rotation: [0, 0, s, s]});
for (let i = 0; i < 240; i++) pills.step(1 / 60);
assert(near(upright.y, .8, .02) && near(lying.y, .3, .02) && lying.sleeping, "capsules at rest");
throws(() => pills.addCapsule({halfHeight: 0}), RangeError, "capsule half height");
pills.dispose();
// Joints: a pendulum keeps its length; a motor turns a hinged wheel.
const joints = new Physics3D.World(null);
const bob = joints.addSphere({position: [1, 5, 0]});
const pin = joints.addBallJoint(bob, null, [0, 5, 0]);
assert(pin.type === "ball" && pin.alive && joints.jointCount === 1, "ball joint");
for (let i = 0; i < 60; i++) joints.step(1 / 60);
assert(near(Math.hypot(bob.x, bob.y - 5, bob.z), 1, .03) && bob.y < 4.9, "pendulum");
const wheel = joints.addBox({halfExtents: [.5, .5, .1], position: [5, 5, 0], angularDamping: 0, linearDamping: 0});
const axle = joints.addHingeJoint(wheel, null, [5, 5, 0], [0, 0, 1], {motorSpeed: 2, maxMotorTorque: 50});
for (let i = 0; i < 60; i++) joints.step(1 / 60);
assert(axle.type === "hinge" && near(wheel.wz, 2, .05) && near(wheel.y, 5, .02), "hinge motor");
axle.setMotor(0, 0).setLimits(-.1, .1);
throws(() => axle.setLimits(1, 0), RangeError, "limits order");
throws(() => pin.setMotor(1, 1), RangeError, "motor needs a hinge");
const rope = joints.addDistanceJoint(joints.addSphere({position: [10, 4, 0]}), null, [10, 4, 0], [10, 5, 0], {length: 2, rope: true});
assert(rope.type === "distance", "distance joint");
const weld = joints.addWeldJoint(bob, wheel, [3, 5, 0]);
assert(weld.type === "weld" && joints.jointCount === 4, "weld joint");
throws(() => joints.addBallJoint(bob, bob, [0, 0, 0]), RangeError, "a != b");
throws(() => joints.addHingeJoint(bob, null, [0, 0, 0], [0, 0, 0]), RangeError, "zero axis");
bob.dispose();
assert(!pin.alive && !weld.alive && joints.jointCount === 2, "joints leave with their body");
rope.remove(); assert(!rope.alive && joints.jointCount === 1, "remove");
axle.dispose(); throws(() => axle.angle, TypeError, "disposed joint");
joints.dispose();
// A world without a level: bodies only collide with each other.
const empty = new Physics3D.World(null, {gravity: [0, 0, 0]});
const a = empty.addSphere({position: [0, 0, 0], velocity: [1, 0, 0]});
const b = empty.addSphere({position: [2, 0, 0]});
for (let i = 0; i < 90; i++) empty.step(1 / 60);
assert(b.vx > .3 && a.vx < .7, "momentum passed on");
empty.dispose();
std.gc();
console.log("Physics3D tests passed (" + checks + " checks)");
