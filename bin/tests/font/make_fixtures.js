// Regenerates the bitmap font fixture: node bin/tests/font/make_fixtures.js
// bitmap.png is a 16x16 grid of 16x16 cells (character code = row * 16 + column);
// each glyph is a white block as wide as its entry in bitmap.dat (256
// little-endian 16-bit widths), so text widths are known in advance.
const fs = require("fs");
const path = require("path");
const zlib = require("zlib");

const dir = __dirname;
const CELL = 16;
const SIZE = CELL * 16;

/* Width of character `code`: 4 to 15 pixels. */
const width = code => 4 + (code % 12);

function crc32(buffer) {
    let crc = ~0;
    for (const byte of buffer) {
        crc ^= byte;
        for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc >>> 0;
}

function chunk(type, body) {
    const header = Buffer.alloc(8);
    header.writeUInt32BE(body.length, 0);
    header.write(type, 4, "ascii");
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([header.subarray(4), body])), 0);
    return Buffer.concat([header, body, crc]);
}

// RGBA rows, each preceded by filter type 0.
const rows = Buffer.alloc(SIZE * (1 + SIZE * 4));
for (let y = 0; y < SIZE; y++) {
    const row = y * (1 + SIZE * 4);
    for (let x = 0; x < SIZE; x++) {
        const code = Math.floor(y / CELL) * 16 + Math.floor(x / CELL);
        const inside = code > 32 && x % CELL < width(code) && y % CELL >= 2 && y % CELL < 14;
        rows.writeUInt32BE(inside ? 0xFFFFFFFF : 0x00000000, row + 1 + x * 4);
    }
}

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(SIZE, 0);
ihdr.writeUInt32BE(SIZE, 4);
ihdr[8] = 8;    // bits per channel
ihdr[9] = 6;    // RGBA
fs.writeFileSync(path.join(dir, "bitmap.png"), Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk("IHDR", ihdr),
    chunk("IDAT", zlib.deflateSync(rows)),
    chunk("IEND", Buffer.alloc(0)),
]));

const widths = Buffer.alloc(256 * 2);
for (let code = 0; code < 256; code++) widths.writeUInt16LE(width(code), code * 2);
fs.writeFileSync(path.join(dir, "bitmap.dat"), widths);
