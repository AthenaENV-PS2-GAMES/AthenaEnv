// Build with screen,loop,gamepad,render3d,voxel,input (plus a boot-device module, e.g. usbmass).
// A generated block world: fly with the left stick, look with the right one,
// CROSS digs and SQUARE places the block under the crosshair. Without input
// the camera circles the island. Run this file as the entry script.
const mode = Screen.getMode();
const viewportHeight = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = viewportHeight;
mode.zbuffering = true;
mode.psmz = Screen.Z16S;
Screen.setMode(mode);

const GRASS = 1, DIRT = 2, STONE = 3, SAND = 4, PLANK = 5;
const world = new Voxel.World({ size: [96, 48, 96], chunk: 16 });
world.setMaterial(GRASS, { color: { top: [0.35, 0.72, 0.25], side: [0.47, 0.36, 0.22], bottom: [0.47, 0.36, 0.22] } });
world.setMaterial(DIRT, { color: [0.47, 0.36, 0.22] });
world.setMaterial(STONE, { color: [0.52, 0.52, 0.55] });
world.setMaterial(SAND, { color: [0.86, 0.8, 0.55] });
world.setMaterial(PLANK, { color: { top: [0.75, 0.55, 0.3], side: [0.65, 0.45, 0.25], bottom: [0.6, 0.42, 0.22] } });
world.generate({ seed: 2024, caves: 0.35, frequency: 1 / 40 });
// A sandy rim around the island.
for (let z = 0; z < 96; z++) for (let x = 0; x < 96; x++) {
    const h = world.surface(x, z);
    if (h <= 16 && h > 0) world.set(x, h - 1, z, SAND);
}
world.rebuild(0);                                   // every chunk, before the first frame

const camera = new Camera3D.Camera({ aspect: mode.width / viewportHeight, near: 0.3, far: 160 });
camera.setViewport(mode.width, viewportHeight);
const controls = new Input.Map({
    move: Input.stick("left", { dpad: true }),
    look: Input.stick("right", { curve: "quadratic", sensitivity: 2 }),
    up: Input.button(Gamepad.R1), down: Input.button(Gamepad.L1),
    dig: Input.button(Gamepad.CROSS), place: Input.button(Gamepad.SQUARE),
});

const eye = { x: 48, y: 40, z: 110 };
let yaw = Math.PI, pitch = -0.45, idle = 0, orbit = 0;
const ray = {}, hit = {}, stats = {};
const SKY = Color.new(110, 160, 220), CROSSHAIR = Color.new(255, 255, 255);

Loop.run({
    update(dt) {
        Gamepad.update();
        controls.update();
        const move = controls.axis("move"), look = controls.axis("look");
        const active = controls.pressed("move") || controls.pressed("look") || controls.pressed("up") || controls.pressed("down");
        idle = active ? 0 : idle + dt;
        if (idle > 3) {                                     // demo: circle the island
            orbit += dt * 0.25;
            eye.x = 48 + Math.sin(orbit) * 70; eye.z = 48 + Math.cos(orbit) * 70; eye.y = 42;
            yaw = Math.atan2(48 - eye.x, 48 - eye.z); pitch = -0.4;
        } else {
            yaw -= look.x * dt; pitch = Math.max(-1.4, Math.min(1.4, pitch - look.y * dt));
            const speed = 12 * dt, fx = Math.sin(yaw), fz = Math.cos(yaw);
            eye.x += (fx * -move.y + fz * move.x) * speed;
            eye.z += (fz * -move.y - fx * move.x) * speed;
            eye.y += ((controls.pressed("up") ? 1 : 0) - (controls.pressed("down") ? 1 : 0)) * speed;
        }
        const cp = Math.cos(pitch);
        camera.setPosition(eye.x, eye.y, eye.z)
            .lookAt(eye.x + Math.sin(yaw) * cp, eye.y + Math.sin(pitch), eye.z + Math.cos(yaw) * cp);

        camera.screenToRay(mode.width / 2, viewportHeight / 2, ray);
        if (world.raycast(ray, 12, hit)) {
            if (controls.justPressed("dig")) world.set(hit.x, hit.y, hit.z, Voxel.AIR);
            else if (controls.justPressed("place")) {
                const x = hit.x + hit.nx, y = hit.y + hit.ny, z = hit.z + hit.nz;
                if (y < world.sizeY && y >= 0) world.set(x, y, z, PLANK);
            }
        }
        world.rebuild(4, eye.x, eye.y, eye.z);              // edited chunks, nearest first
    },
    draw() {
        Screen.clear(SKY);
        world.draw(camera, Render3D.CULL_BACK, undefined, stats, 120);
        const cx = mode.width / 2, cy = viewportHeight / 2;
        Draw.line(cx - 6, cy, cx + 6, cy, CROSSHAIR);
        Draw.line(cx, cy - 6, cx, cy + 6, CROSSHAIR);
    },
});
