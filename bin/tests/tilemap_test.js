function check(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function expectThrow(callback, type, message) {
    let error;
    try {
        callback();
    } catch (caught) {
        error = caught;
    }
    check(error instanceof type, message);
}

const layout = TileMap.layout;
check(layout.stride === 64 && layout.offsets.x === 0 &&
    layout.offsets.u1 === 16 && layout.offsets.r === 32 &&
    layout.offsets.zindex === 56,
    "TileMap.layout does not match the native sprite record");

// Descriptor validation.
expectThrow(() => new TileMap.Descriptor(), TypeError,
    "Descriptor accepted no options");
expectThrow(() => new TileMap.Descriptor({ materials: [] }), RangeError,
    "Descriptor accepted no materials");
expectThrow(() => new TileMap.Descriptor({ materials: [{}] }), RangeError,
    "Descriptor accepted a material without endOffset");
expectThrow(() => new TileMap.Descriptor({
    materials: [{ endOffset: 4 }, { endOffset: 2 }],
}), RangeError, "Descriptor accepted materials out of order");
expectThrow(() => new TileMap.Descriptor({
    materials: [{ textureIndex: 0, endOffset: 0 }],
}), RangeError, "Descriptor accepted a textureIndex without textures");
expectThrow(() => new TileMap.Descriptor({
    textures: [42], materials: [{ endOffset: 0 }],
}), TypeError, "Descriptor accepted a non-image texture");
expectThrow(() => new TileMap.Descriptor({
    textures: ["host:/missing-atlas.png"], materials: [{ endOffset: 0 }],
}), Error, "Descriptor accepted a missing texture file");

const texture = new Image("tests/texture.png");
const tileW = texture.width / 2;
const tileH = texture.height / 2;
const descriptor = new TileMap.Descriptor({
    textures: [texture, "tests/texture.png"],
    materials: [
        { endOffset: 11 },
        {
            textureIndex: -1,
            blendMode: Screen.alphaEquation(Screen.ZERO_RGB, Screen.SRC_RGB,
                Screen.SRC_ALPHA, Screen.DST_RGB, 0),
            endOffset: 13,
        },
    ],
});
check(descriptor.materialCount === 2 && descriptor.textures.length === 2 &&
    descriptor.textures[0] === texture &&
    descriptor.textures[1] instanceof Image,
    "Descriptor did not keep its textures");

// Sprite buffers.
expectThrow(() => TileMap.SpriteBuffer.create(0), RangeError,
    "SpriteBuffer.create accepted zero sprites");
expectThrow(() => TileMap.SpriteBuffer.fromObjects([{ r: 256 }]), RangeError,
    "SpriteBuffer.fromObjects accepted an out-of-range color");
expectThrow(() => TileMap.SpriteBuffer.fromObjects([{ x: NaN }]), RangeError,
    "SpriteBuffer.fromObjects accepted a non-finite coordinate");

const sprites = [];
for (let y = 0; y < 3; y++) {
    for (let x = 0; x < 4; x++) {
        const u = (x % 2) * tileW;
        const v = (y % 2) * tileH;
        sprites.push({
            x: x * tileW, y: y * tileH, w: tileW, h: tileH, zindex: 1,
            u1: u, v1: v, u2: u + tileW, v2: v + tileH,
        });
    }
}
// Two untextured, half-transparent markers for the second material.
sprites.push({ x: 0, y: 3 * tileH + 8, w: 32, h: 16, zindex: 1,
    r: 255, g: 64, b: 64, a: 64 });
sprites.push({ x: 40, y: 3 * tileH + 8, w: 32, h: 16, zindex: 1,
    r: 64, g: 255, b: 64, a: 64 });

const buffer = TileMap.SpriteBuffer.fromObjects(sprites);
const view = new DataView(buffer);
check(buffer.byteLength === sprites.length * layout.stride &&
    view.getFloat32(layout.stride + layout.offsets.x, true) === tileW &&
    view.getUint32(layout.offsets.r, true) === 128,
    "SpriteBuffer.fromObjects wrote unexpected data");

// Instance validation and buffer management.
expectThrow(() => new TileMap.Instance({ descriptor: {} }), TypeError,
    "Instance accepted a non-descriptor");
expectThrow(() => new TileMap.Instance({
    descriptor, spriteBuffer: new ArrayBuffer(10),
}), RangeError, "Instance accepted a partial sprite record");

const map = new TileMap.Instance({ descriptor, spriteBuffer: buffer });
check(map.spriteCount === sprites.length && map.getSpriteBuffer() === buffer &&
    map.descriptor === descriptor, "Instance did not keep its buffer");
expectThrow(() => map.render(0), TypeError,
    "Instance.render accepted a missing y");
expectThrow(() => map.render(0, 0, 1), TypeError,
    "Instance.render accepted an origin z, which the VU program ignores");

const empty = new TileMap.Instance({ descriptor });
check(empty.spriteCount === 0 && empty.getSpriteBuffer() === undefined,
    "Instance without a buffer reported sprites");
empty.render(0, 0);

const patch = TileMap.SpriteBuffer.create(1);
new DataView(patch).setFloat32(layout.offsets.w, 99, true);
map.updateSprites(12, patch);
check(view.getFloat32(12 * layout.stride + layout.offsets.w, true) === 99,
    "Instance.updateSprites did not copy the sprite");
expectThrow(() => map.updateSprites(sprites.length, patch), RangeError,
    "Instance.updateSprites accepted an offset past the buffer");
map.updateSprites(12, TileMap.SpriteBuffer.fromObjects([sprites[12]]));

map.render(0, 0);
const replacement = TileMap.SpriteBuffer.fromObjects(sprites);
map.replaceSpriteBuffer(replacement);
check(map.getSpriteBuffer() === replacement,
    "Instance.replaceSpriteBuffer did not switch buffers");

TileMap.setCamera(12.5, -4);
check(TileMap.getCamera().x === 12.5 && TileMap.getCamera().y === -4,
    "TileMap camera was not stored");
TileMap.setCamera(0, 0);

console.log("TileMap tests passed");

// Visual check: an animated tile grid and two translucent markers.
const replacementView = new DataView(replacement);
let time = 0;
while (true) {
    time += 0.05;
    for (let i = 0; i < 12; i++) {
        replacementView.setFloat32(i * layout.stride + layout.offsets.y,
            sprites[i].y + Math.sin(time + i * 0.5) * 4, true);
    }
    Screen.clear(0x80182030);
    map.render(64, 48);
    Screen.flip();
}
