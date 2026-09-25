// Reference encodings of tools/wav2adp.js, which tests/host/run.sh compares
// with the C port (tools/wav2adp/wav2adp.c) byte for byte.
// Regenerate after changing the encoder: node tests/host/wav2adp/make_refs.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodeWav } from '../../../tools/wav2adp.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const fixtures = path.join(here, '../../../bin/tests/sound');

/* WAV of a 440 Hz tone plus a quieter 3 kHz one, as `bits`-bit PCM or float. */
function tone(rate, channels, bits, ms, float = false) {
    const frames = Math.floor(rate * ms / 1000);
    const bytes = bits / 8;
    const data = Buffer.alloc(frames * channels * bytes);
    for (let i = 0; i < frames; i++) {
        for (let c = 0; c < channels; c++) {
            const v = 0.6 * Math.sin(2 * Math.PI * 440 * i / rate + c) +
                0.2 * Math.sin(2 * Math.PI * 3000 * i / rate);
            const at = (i * channels + c) * bytes;
            if (float) data.writeFloatLE(v, at);
            else if (bits === 24) data.writeIntLE(Math.round(v * 8388607), at, 3);
            else if (bits === 32) data.writeInt32LE(Math.round(v * 2147483647), at);
            else data.writeInt16LE(Math.round(v * 32767), at);
        }
    }
    const fmt = Buffer.alloc(16);
    fmt.writeUInt16LE(float ? 3 : 1, 0);
    fmt.writeUInt16LE(channels, 2);
    fmt.writeUInt32LE(rate, 4);
    fmt.writeUInt32LE(rate * channels * bytes, 8);
    fmt.writeUInt16LE(channels * bytes, 12);
    fmt.writeUInt16LE(bits, 14);
    const chunk = (id, body) => {
        const h = Buffer.alloc(8);
        h.write(id, 0, 'ascii');
        h.writeUInt32LE(body.length, 4);
        return Buffer.concat([h, body, body.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0)]);
    };
    const body = Buffer.concat([Buffer.from('WAVE'), chunk('fmt ', fmt), chunk('data', data)]);
    const riff = Buffer.alloc(8);
    riff.write('RIFF', 0, 'ascii');
    riff.writeUInt32LE(body.length, 4);
    return Buffer.concat([riff, body]);
}

// Inputs the Sound fixtures do not cover.
fs.writeFileSync(path.join(here, 'pcm24.wav'), tone(32000, 2, 24, 150));
fs.writeFileSync(path.join(here, 'pcm32.wav'), tone(22050, 1, 32, 150));

const inputs = {
    short: path.join(fixtures, 'short.wav'),       // 16-bit mono, LIST chunk
    rate16k: path.join(fixtures, 'rate16k.wav'),   // 16-bit mono
    stereo8: path.join(fixtures, 'stereo8.wav'),   // 8-bit stereo: mixed down
    float: path.join(fixtures, 'float.wav'),       // 32-bit float stereo
    pcm24: path.join(here, 'pcm24.wav'),
    pcm32: path.join(here, 'pcm32.wav'),
};
for (const [name, file] of Object.entries(inputs)) {
    const wav = fs.readFileSync(file);
    fs.writeFileSync(path.join(here, `${name}.adp`), encodeWav(wav));
    fs.writeFileSync(path.join(here, `${name}.loop.adp`), encodeWav(wav, { loop: true }));
}
console.log(`${Object.keys(inputs).length * 2} reference files written to ${here}`);
