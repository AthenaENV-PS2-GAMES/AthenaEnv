// Regenerates the Sound test fixtures: node bin/tests/sound/make_fixtures.js
// music.ogg is a copy of old/bin/sounds/guitar-improvisation-59465.ogg.
const fs = require("fs");
const path = require("path");

const dir = __dirname;

function chunk(id, body) {
    const header = Buffer.alloc(8);
    header.write(id, 0, "ascii");
    header.writeUInt32LE(body.length, 4);
    const pad = body.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0);
    return Buffer.concat([header, body, pad]);
}

function fmt(tag, channels, rate, bits) {
    const body = Buffer.alloc(16);
    body.writeUInt16LE(tag, 0);
    body.writeUInt16LE(channels, 2);
    body.writeUInt32LE(rate, 4);
    body.writeUInt32LE(rate * channels * bits / 8, 8);
    body.writeUInt16LE(channels * bits / 8, 12);
    body.writeUInt16LE(bits, 14);
    return body;
}

/* 440 Hz sine, 16-bit. */
function tone(rate, channels, ms) {
    const frames = Math.floor(rate * ms / 1000);
    const data = Buffer.alloc(frames * channels * 2);
    for (let i = 0; i < frames; i++) {
        const sample = Math.round(Math.sin(2 * Math.PI * 440 * i / rate) * 8000);
        for (let c = 0; c < channels; c++)
            data.writeInt16LE(sample, (i * channels + c) * 2);
    }
    return data;
}

function wav(name, chunks) {
    const body = Buffer.concat([Buffer.from("WAVE", "ascii"), ...chunks]);
    const riff = Buffer.alloc(8);
    riff.write("RIFF", 0, "ascii");
    riff.writeUInt32LE(body.length, 4);
    fs.writeFileSync(path.join(dir, name), Buffer.concat([riff, body]));
}

// 500 ms, 22050 Hz mono, with a LIST chunk before "data" and an 18-byte fmt.
wav("short.wav", [
    chunk("fmt ", Buffer.concat([fmt(1, 1, 22050, 16), Buffer.alloc(2)])),
    chunk("LIST", Buffer.from("INFOISFT\x05\x00\x00\x00test\x00", "binary")),
    chunk("data", tone(22050, 1, 500)),
]);
// audsrv has no 16000 Hz upsampler.
wav("rate16k.wav", [chunk("fmt ", fmt(1, 1, 16000, 16)), chunk("data", tone(16000, 1, 100))]);
// IEEE float samples.
wav("float.wav", [chunk("fmt ", fmt(3, 2, 44100, 32)), chunk("data", Buffer.alloc(4096))]);
// Header with no "data" chunk.
wav("nodata.wav", [chunk("fmt ", fmt(1, 2, 44100, 16))]);

fs.writeFileSync(path.join(dir, "garbage.bin"), Buffer.from("this is not audio at all, just text\n"));
// APCM-sized file with the wrong magic.
const badAdpcm = Buffer.alloc(64);
badAdpcm.write("XPCM", 0, "ascii");
fs.writeFileSync(path.join(dir, "bad.adp"), badAdpcm);
