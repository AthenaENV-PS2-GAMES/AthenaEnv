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
/* 440 Hz sine as 8-bit unsigned or 32-bit float samples. */
function toneAs(rate, channels, ms, kind) {
    const frames = Math.floor(rate * ms / 1000);
    const size = kind === "u8" ? 1 : 4;
    const data = Buffer.alloc(frames * channels * size);
    for (let i = 0; i < frames; i++) {
        const value = Math.sin(2 * Math.PI * 440 * i / rate) * 0.25;
        for (let c = 0; c < channels; c++) {
            const at = (i * channels + c) * size;
            if (kind === "u8") data[at] = 128 + Math.round(value * 127);
            else data.writeFloatLE(value, at);
        }
    }
    return data;
}

// Formats audsrv cannot play directly: converted on the EE.
// 16000 Hz (audsrv has no upsampler for it): played at 32000 Hz.
wav("rate16k.wav", [chunk("fmt ", fmt(1, 1, 16000, 16)), chunk("data", tone(16000, 1, 2000))]);
// IEEE float samples.
wav("float.wav", [chunk("fmt ", fmt(3, 2, 44100, 32)), chunk("data", toneAs(44100, 2, 1000, "f32"))]);
// 8-bit stereo exists in audsrv only at 11025 Hz.
wav("stereo8.wav", [chunk("fmt ", fmt(1, 2, 22050, 8)), chunk("data", toneAs(22050, 2, 1000, "u8"))]);
// Still refused: mu-law and more than two channels.
wav("mulaw.wav", [chunk("fmt ", fmt(7, 1, 8000, 8)), chunk("data", Buffer.alloc(800))]);
wav("surround.wav", [chunk("fmt ", fmt(1, 6, 48000, 16)), chunk("data", Buffer.alloc(4800))]);
// Header with no "data" chunk.
wav("nodata.wav", [chunk("fmt ", fmt(1, 2, 44100, 16))]);

fs.writeFileSync(path.join(dir, "garbage.bin"), Buffer.from("this is not audio at all, just text\n"));
// APCM-sized file with the wrong magic.
const badAdpcm = Buffer.alloc(64);
badAdpcm.write("XPCM", 0, "ascii");
fs.writeFileSync(path.join(dir, "bad.adp"), badAdpcm);

// Sound effects made with tools/wav2adp.js (same encoder as adpenc).
const { encodeWav } = require("../../../tools/wav2adp.js");
const wavBytes = (rate, ms) => {
    const data = tone(rate, 1, ms);
    const riff = Buffer.concat([Buffer.from("WAVE", "ascii"),
        chunk("fmt ", fmt(1, 1, rate, 16)), chunk("data", data)]);
    const header = Buffer.alloc(8);
    header.write("RIFF", 0, "ascii");
    header.writeUInt32LE(riff.length, 4);
    return Buffer.concat([header, riff]);
};
// ~1.9 s, mono: 23744 bytes of SPU2 memory (1483 blocks + the end block), so
// 100 loads (the free() and GC tests) need ~2.3 MiB: more than SPU2 RAM.
const OVER_FRAMES = 1483 * 28;
const overAdpcm = encodeWav(wavBytes(22050, OVER_FRAMES * 1000 / 22050));
if (overAdpcm.length - 16 !== 23744) throw new Error("over.adp: " + (overAdpcm.length - 16) + " bytes");
fs.writeFileSync(path.join(dir, "over.adp"), overAdpcm);
// 200 ms that loop forever.
const loopAdpcm = encodeWav(wavBytes(22050, 200), { loop: true });
fs.writeFileSync(path.join(dir, "loop.adp"), loopAdpcm);
// Cut in the middle of a block: the SPU2 would never find the end flag.
const oneShot = encodeWav(wavBytes(22050, 200));
fs.writeFileSync(path.join(dir, "truncated.adp"), oneShot.subarray(0, oneShot.length - 24));
// Whole blocks, but the end flags removed from the last two.
const noEnd = Buffer.from(oneShot);
noEnd[noEnd.length - 16 + 1] = 0;
noEnd[noEnd.length - 32 + 1] = 0;
fs.writeFileSync(path.join(dir, "noend.adp"), noEnd);
