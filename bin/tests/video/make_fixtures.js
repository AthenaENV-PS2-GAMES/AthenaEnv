// Regenerates the Video test fixtures: node bin/tests/video/make_fixtures.js
// short.m2v is the first 4 GOPs (60 frames, 640x360 @ 60 fps, MPEG-2 4:2:0)
// of old/bin/video.m2v, closed with a sequence end code. The other streams
// are short.m2v with header fields patched or data cut off.
const fs = require("fs");
const path = require("path");

const dir = __dirname;
const source = fs.readFileSync(path.join(dir, "..", "..", "..", "old", "bin", "video.m2v"));

const PICTURE = 0x00;
const SEQUENCE_HEADER = 0xB3;
const EXTENSION = 0xB5;
const SEQUENCE_END = 0xB7;
const GOPS = 4;

function startCodes(buffer, code) {
    const offsets = [];
    for (let i = 0; i + 3 < buffer.length; i++) {
        if (buffer[i] === 0 && buffer[i + 1] === 0 && buffer[i + 2] === 1 && buffer[i + 3] === code)
            offsets.push(i);
    }
    return offsets;
}

function write(name, data) {
    fs.writeFileSync(path.join(dir, name), data);
}

// Applies `patch(buffer, offset)` to a copy, at every sequence header (the
// stream repeats it before each GOP).
function patchHeaders(buffer, patch) {
    const copy = Buffer.from(buffer);
    for (const offset of startCodes(copy, SEQUENCE_HEADER))
        patch(copy, offset);
    return copy;
}

const headers = startCodes(source, SEQUENCE_HEADER);
if (headers.length <= GOPS) throw new Error("source stream is too short");
const body = source.subarray(0, headers[GOPS]);
const end = Buffer.from([0, 0, 1, SEQUENCE_END]);
const short = Buffer.concat([body, end]);

write("short.m2v", short);
write("garbage.bin", Buffer.from("this is not an MPEG stream\n".repeat(64)));
write("empty.m2v", Buffer.alloc(0));

// No sequence end code: the decoder must see the end of the data itself.
write("noend.m2v", body);

// Cut halfway through the 30th picture, without an end code.
const pictures = startCodes(short, PICTURE);
write("truncated.m2v", short.subarray(0, (pictures[29] + pictures[30]) >> 1));

// horizontal_size_value = 1280: over the 1024 GS texture limit.
write("wide.m2v", patchHeaders(short, (b, o) => {
    b[o + 4] = 1280 >> 4;
    b[o + 5] = ((1280 & 0x0F) << 4) | (b[o + 5] & 0x0F);
}));

// frame_rate_code = 0 (forbidden).
write("badrate.m2v", patchHeaders(short, (b, o) => {
    b[o + 7] &= 0xF0;
}));

// chroma_format = 4:2:2 in every sequence extension.
const chroma = Buffer.from(short);
for (const offset of startCodes(chroma, EXTENSION)) {
    if (chroma[offset + 4] >> 4 === 1)
        chroma[offset + 5] = (chroma[offset + 5] & ~0x06) | (2 << 1);
}
write("chroma422.m2v", chroma);

// short.wav: 1 s of a quiet 440 Hz tone (44.1 kHz, 16-bit mono), the same
// length as short.m2v, for the audio sync tests.
{
    const rate = 44100;
    const samples = rate;
    const data = Buffer.alloc(samples * 2);
    for (let i = 0; i < samples; i++)
        data.writeInt16LE(Math.round(3000 * Math.sin(2 * Math.PI * 440 * i / rate)), i * 2);
    const header = Buffer.alloc(44);
    header.write("RIFF", 0, "ascii");
    header.writeUInt32LE(36 + data.length, 4);
    header.write("WAVEfmt ", 8, "ascii");
    header.writeUInt32LE(16, 16);
    header.writeUInt16LE(1, 20);        // PCM
    header.writeUInt16LE(1, 22);        // mono
    header.writeUInt32LE(rate, 24);
    header.writeUInt32LE(rate * 2, 28);
    header.writeUInt16LE(2, 32);
    header.writeUInt16LE(16, 34);
    header.write("data", 36, "ascii");
    header.writeUInt32LE(data.length, 40);
    write("short.wav", Buffer.concat([header, data]));
}
