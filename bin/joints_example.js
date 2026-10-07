// Physics3D joints on PS2: a 10-link chain hanging from the world (ball
// joints), a windmill turned by a hinge motor, a door on a hinge with
// limits (+-60 degrees) hit by a ball every 2 s, and a rope pendulum. Prints
// JOINTS_PROFILE with the step and draw ticks (1 tick = 1 us) over 540
// frames after a 60-frame warm-up.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 80});
camera.setPosition(0, 6, 16).lookAt(0, 3, 0);
const lights = new Lights.Set().setAmbient(.35, .35, .35).setDirectional(0, .3, .8, .5, .9, .9, .85);
const FACES = [0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,1,5, 0,5,4, 2,6,7, 2,7,3, 0,4,6, 0,6,2, 1,3,7, 1,7,5];
function box(out, min, max) { for (const i of FACES) out.push(i & 1 ? max[0] : min[0], i & 2 ? max[1] : min[1], i & 4 ? max[2] : min[2]); }
function mesh(min, max, color) {
    const p = []; box(p, min, max);
    return Model3D.Mesh.fromGeometry({positions: new Float32Array(p), material: {shading: Model3D.DIFFUSE, baseColor: new Float32Array(color)}});
}
const floorMesh = mesh([-12, -1, -6], [12, 0, 6], [.55, .6, .65, 1]);
const linkMesh = mesh([-.15, -.25, -.15], [.15, .25, .15], [.9, .7, .2, 1]);
const bladeMesh = mesh([-2, -.15, -.1], [2, .15, .1], [.3, .7, 1, 1]);
const doorMesh = mesh([-.8, -1.2, -.08], [.8, 1.2, .08], [.7, .45, .3, 1]);
const ballMesh = mesh([-.3, -.3, -.3], [.3, .3, .3], [1, .3, .3, 1]);
const scene = new Scene3D.Scene(), root = scene.root;
const floorNode = new Scene3D.Node().setMesh(floorMesh); root.add(floorNode);
const level = new Collision3D.World(); level.addNode(floorNode);
const world = new Physics3D.World(level);
function attach(body, m) { const n = new Scene3D.Node().setMesh(m); root.add(n); body.bind(n); return body; }
// Chain: links 0.5 long hanging from (-7, 8, 0).
let previous = null;
for (let i = 0; i < 10; i++) {
    const link = attach(world.addBox({halfExtents: [.15, .25, .15], mass: .3, position: [-7 + i * .5, 7.75, 0]}), linkMesh);
    world.addBallJoint(link, previous, previous ? [-7 + i * .5 - .25, 7.75, 0] : [-7.25, 7.75, 0]);
    previous = link;
}
// Windmill: two crossed blades welded, on a motorized hinge about z.
const blade = attach(world.addBox({halfExtents: [2, .15, .1], mass: 2, position: [0, 6, 0], angularDamping: 0}), bladeMesh);
const blade2 = attach(world.addBox({halfExtents: [2, .15, .1], mass: 2, position: [0, 6, 0], rotation: [0, 0, Math.SQRT1_2, Math.SQRT1_2], angularDamping: 0}), bladeMesh);
world.addWeldJoint(blade2, blade, [0, 6, 0]);
world.addHingeJoint(blade, null, [0, 6, 0], [0, 0, 1], {motorSpeed: 1.5, maxMotorTorque: 200});
// Door on a vertical hinge with limits, and a ball thrown at it.
const door = attach(world.addBox({halfExtents: [.8, 1.2, .08], mass: 3, position: [6.8, 1.25, 0]}), doorMesh);
const doorJoint = world.addHingeJoint(door, null, [6, 1.25, 0], [0, 1, 0], {lower: -Math.PI / 3, upper: Math.PI / 3});
const ball = attach(world.addSphere({radius: .3, mass: 1, position: [6.8, 1, 4]}), ballMesh);
// Rope pendulum.
const weight = attach(world.addSphere({radius: .3, position: [10, 4, 0]}), ballMesh);
world.addDistanceJoint(weight, null, [10, 4, 0], [9, 7, 0], {rope: true});
const FRAMES = 600, WARMUP = 60, timer = Timer.new(), stats = {};
let frame = 0, steps = [], draws = [];
console.log("Joints stage 0: joints");
Loop.run({
    update() {
        if (frame % 120 === 0) { ball.setPosition(6.8, 1, 4); ball.setVelocity(0, 1.5, -9); }
        const start = Timer.getTime(timer);
        world.step(1 / 60);
        if (frame >= WARMUP) steps.push(Timer.getTime(timer) - start);
        scene.update();
    },
    draw() {
        const start = Timer.getTime(timer);
        scene.draw(camera, Render3D.CULL_BACK, lights, stats);
        if (frame >= WARMUP) draws.push(Timer.getTime(timer) - start);
        if (++frame === FRAMES) {
            const mean = v => +(v.reduce((a, b) => a + b, 0) / v.length).toFixed(1);
            console.log("JOINTS_PROFILE " + JSON.stringify({samples: steps.length, stepTicksMean: mean(steps),
                drawTicksMean: mean(draws), bodies: world.bodyCount, joints: world.jointCount,
                contacts: world.contactCount, doorAngle: +doorJoint.angle.toFixed(3)}));
            Timer.destroy(timer); Loop.stop();
        }
    }
});
