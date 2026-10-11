// Physics3D behaviour checks on the PS2 itself (the VU0 contact solver only
// runs there; the host tests run its C twin). Prints PHYSICS3D_CHECK lines
// and a summary, then stops.
const results = [];
function check(ok, label) { results.push(ok); console.log("PHYSICS3D_CHECK " + (ok ? "PASS " : "FAIL ") + label); }
const near = (a, b, e) => Math.abs(a - b) <= e;
const level = new Collision3D.World(); level.addBox([-20, -1, -20], [20, 0, 20]);
const world = new Physics3D.World(level);
const ball = world.addSphere({radius: .5, position: [0, 4, 0]});
const pill = world.addCapsule({radius: .3, halfHeight: .5, position: [3, 2, 0], rotation: [0, 0, Math.SQRT1_2, Math.SQRT1_2]});
const stack = [0, 1, 2].map(i => world.addBox({halfExtents: [.5, .5, .5], position: [8, .5 + i * 1.01, 0]}));
const slider = world.addBox({halfExtents: [.5, .5, .5], position: [-8, .5, 0]});
const bouncy = world.addSphere({radius: .5, position: [-4, 5, 4], restitution: .8});
let bounced = false, peak = 0;
for (let i = 0; i < 300; i++) {
    if (i === 30) slider.applyImpulse(5, 0, 0);
    world.step(1 / 60);
    if (bouncy.vy > 0) { bounced = true; peak = Math.max(peak, bouncy.y); }
}
check(near(ball.y, .5, .02) && ball.sleeping, "ball rests at its radius and sleeps (y " + ball.y.toFixed(3) + ")");
check(near(pill.y, .3, .02), "lying capsule rests at its radius (y " + pill.y.toFixed(3) + ")");
check(stack.every((b, i) => near(b.y, .5 + i, .05) && near(b.x, 8, .05)), "3-box stack stands (" + stack.map(b => b.y.toFixed(2)).join(" ") + ")");
check(stack.every(b => b.sleeping), "the stack sleeps as one island");
check(slider.x > -7 && slider.x < -3 && near(slider.vx, 0, .01), "friction stops a pushed box (x " + slider.x.toFixed(2) + ")");
check(bounced && peak > 2.5, "restitution bounces (peak " + peak.toFixed(2) + ")");
// Pendulum on a ball joint keeps its length.
const free = new Physics3D.World(null);
const bob = free.addSphere({position: [1, 5, 0]});
free.addBallJoint(bob, null, [0, 5, 0]);
let worst = 0;
for (let i = 0; i < 120; i++) { free.step(1 / 60); worst = Math.max(worst, Math.abs(Math.hypot(bob.x, bob.y - 5, bob.z) - 1)); }
check(worst < .03, "pendulum keeps its length (worst error " + worst.toFixed(3) + ")");
const passed = results.filter(x => x).length;
console.log("PHYSICS3D_CHECK " + passed + "/" + results.length + " passed; Physics3D check complete;");
world.dispose(); free.dispose(); level.dispose();
Loop.run({ update() { Loop.stop(); } });
