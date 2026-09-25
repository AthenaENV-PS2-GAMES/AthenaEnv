// Box2D example: a small physics playground drawn with Box2DDraw.
//
//   Left stick  move the player (capsule)
//   CROSS       jump (only when standing on something)
//   SQUARE      drop a crate
//   CIRCLE      explosion under the player
//   TRIANGLE    snapshot / restore the world
//   SELECT      toggle contacts and bounds
//   START       quit
//
// Build with the modules: node tools/modules.js configure --modules=box2d,box2ddraw,gamepad,font,screen,timer,...

const SCALE = 24;           // pixels per meter
const WIDTH = 640, HEIGHT = 448;
const WHITE = Color.new(255, 255, 255);
const BLACK = Color.new(0, 0, 0);

const world = Box2D.createWorld({ gravity: { x: 0, y: -20 } });

// Ground and walls, with a hill as a chain (listed right to left: solid side up).
const ground = world.createBody();
ground.createBoxShape({ halfWidth: 13, halfHeight: 0.5, center: { x: 0, y: -0.5 } });
ground.createBoxShape({ halfWidth: 0.5, halfHeight: 9, center: { x: -13.5, y: 8 } });
ground.createBoxShape({ halfWidth: 0.5, halfHeight: 9, center: { x: 13.5, y: 8 } });
ground.createChain([{ x: 12, y: 0 }, { x: 9, y: 0 }, { x: 6, y: 2 }, { x: 3, y: 0 }, { x: 0, y: 0 }]);

// A pendulum on a revolute joint, and a seesaw.
const pivot = world.createBody({ position: { x: -7, y: 12 } });
const bob = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: -4, y: 12 } });
bob.createCircleShape({ radius: 0.6, density: 3 });
world.createDistanceJoint(pivot, bob);
const plank = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: -5, y: 1.2 } });
plank.createBoxShape({ halfWidth: 3, halfHeight: 0.15 });
world.createRevoluteJoint(ground, plank, { anchor: { x: -5, y: 1.2 } });

// The player: a capsule that does not tip over.
const player = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: 0, y: 3 }, fixedRotation: true });
player.createCapsuleShape({ point1: { x: 0, y: -0.4 }, point2: { x: 0, y: 0.4 }, radius: 0.4, friction: 0.2 });

const crates = [];
function dropCrate() {
    const crate = world.createBody({ type: Box2D.DYNAMIC_BODY, position: { x: Math.random() * 16 - 8, y: 16 } });
    crate.createBoxShape({ halfWidth: 0.5, halfHeight: 0.5, restitution: 0.2 });
    crates.push(crate);
}
for (let i = 0; i < 8; i++) dropCrate();

// Standing on something: a contact whose normal points up at the player.
function grounded() {
    return player.getContacts().some(c => {
        const ny = c.shapeA.getBody() === player ? -c.normal.y : c.normal.y;
        return ny > 0.5;
    });
}

const font = new Font();
const pad = Gamepad.player(0);
let details = false;
let saved = null;
let message = "";

while (true) {
    Gamepad.update();
    if (pad.justPressed(Gamepad.START)) break;
    if (pad.justPressed(Gamepad.SELECT)) details = !details;
    if (pad.justPressed(Gamepad.SQUARE) && crates.length < 60) dropCrate();
    if (pad.justPressed(Gamepad.CIRCLE)) {
        const p = player.getPosition();
        world.explode(p.x, p.y - 1, 2, 3, 20);
    }
    if (pad.justPressed(Gamepad.TRIANGLE)) {
        if (saved) {
            world.restore(saved);
            saved = null;
            message = "restored";
        } else {
            saved = world.snapshot();
            message = "snapshot: " + saved.byteLength + " bytes";
        }
    }

    // Horizontal control by velocity; jump by impulse when grounded.
    const v = player.getLinearVelocity();
    player.setLinearVelocity(pad.leftX * 8, v.y);
    if (pad.justPressed(Gamepad.CROSS) && grounded())
        player.applyLinearImpulseToCenter(0, 9 * player.getMass());

    world.step(1 / 60, 4);

    Screen.clear(BLACK);
    Box2DDraw.draw(world, { scale: SCALE, offsetX: WIDTH / 2, offsetY: HEIGHT - 24,
        contacts: details, bounds: details });
    const profile = world.getProfile();
    font.print(10, 8, "step " + profile.step.toFixed(2) + " ms  bodies awake " + world.getAwakeBodyCount() +
        "  Box2D " + (Box2D.getMemoryUsage() >> 10) + " KB");
    font.print(10, 28, message);
    Screen.flip();
}

world.destroy();
console.log("Box2D example finished");
