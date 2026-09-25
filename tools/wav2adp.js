#!/usr/bin/env node
/**
 * wav2adp: converts WAV files to the .adp sound effects Sound.Sfx loads
 * (SPU2 ADPCM with an APCM header), with no PS2SDK install.
 *
 * The encoder is a port of the PS2SDK's adpenc (tools/adpenc/src/adpcm.c):
 * a 16-bit mono WAV produces the same bytes. Unlike adpenc it also reads
 * 8/24/32-bit PCM and 32-bit float WAVs, and mixes stereo down to mono,
 * since audsrv plays a single voice per sample (adpenc stores the two
 * channels one after the other and only the first is ever heard).
 *
 * Usage:
 *   node tools/wav2adp.js [-L] input.wav [output.adp]
 *   node tools/wav2adp.js [-L] --dir assets/sfx     (every .wav whose .adp is missing or older)
 *
 *   -L, --loop   the sample loops forever (Sfx.loop is true)
 *
 * Zero dependencies; also works with Bun. Import `encodeWav` or `encodeAdpcm`
 * to use it from a build script.
 */

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const BLOCK_SAMPLES = 28;
/* audsrv voices: pitch register 0x1000 = 48 kHz, 14 bits. */
const MAX_PITCH = 0x3fff;

const FILTERS = [
    [0.0, 0.0],
    [-60.0 / 64.0, 0.0],
    [-115.0 / 64.0, 52.0 / 64.0],
    [-98.0 / 64.0, 55.0 / 64.0],
    [-122.0 / 64.0, 60.0 / 64.0],
];

/** Parses a RIFF/WAVE file into mono 16-bit samples. */
export function readWav(buffer) {
    if (buffer.length < 12 || buffer.toString('ascii', 0, 4) !== 'RIFF' ||
        buffer.toString('ascii', 8, 12) !== 'WAVE')
        throw new Error('not a WAV file');

    let format = null;
    let data = null;
    for (let offset = 12; offset + 8 <= buffer.length;) {
        const id = buffer.toString('ascii', offset, offset + 4);
        const size = buffer.readUInt32LE(offset + 4);
        const body = offset + 8;
        if (id === 'fmt ') {
            let tag = buffer.readUInt16LE(body);
            // WAVE_FORMAT_EXTENSIBLE: the real tag opens the sub-format GUID.
            if (tag === 0xfffe && size >= 26) tag = buffer.readUInt16LE(body + 24);
            format = {
                tag,
                channels: buffer.readUInt16LE(body + 2),
                rate: buffer.readUInt32LE(body + 4),
                bits: buffer.readUInt16LE(body + 14),
            };
        } else if (id === 'data') {
            // Streamed writers leave 0 or 0xFFFFFFFF: the data runs to the end.
            const end = size === 0 || body + size > buffer.length ? buffer.length : body + size;
            data = buffer.subarray(body, end);
            break;
        }
        offset = body + size + (size & 1);
    }
    if (!format) throw new Error('no "fmt " chunk');
    if (!data) throw new Error('no "data" chunk');

    const { tag, channels, rate, bits } = format;
    const pcm = tag === 1 && [8, 16, 24, 32].includes(bits);
    const float = tag === 3 && bits === 32;
    if (!pcm && !float)
        throw new Error(`WAV encoding 0x${tag.toString(16)} with ${bits}-bit samples is not supported`);
    if (channels < 1) throw new Error('no channels');
    const pitch = Math.floor((rate * 4096) / 48000);
    if (pitch < 1 || pitch > MAX_PITCH)
        throw new Error(`${rate} Hz is out of range for the SPU2 (12..191999 Hz)`);

    const sampleBytes = bits / 8;
    const frameBytes = sampleBytes * channels;
    const frames = Math.floor(data.length / frameBytes);
    const read = (offset) => {
        if (float) {
            const value = data.readFloatLE(offset);
            if (!(value > -1)) return value <= -1 ? -32768 : 0;
            return value >= 1 ? 32767 : Math.trunc(value * 32767);
        }
        switch (bits) {
        case 8: return (data[offset] - 128) << 8;
        case 16: return data.readInt16LE(offset);
        case 24: return data.readInt16LE(offset + 1);
        default: return data.readInt16LE(offset + 2);
        }
    };

    const samples = new Int16Array(frames);
    for (let i = 0; i < frames; i++) {
        let sum = 0;
        for (let c = 0; c < channels; c++) sum += read(i * frameBytes + c * sampleBytes);
        samples[i] = Math.round(sum / channels);
    }
    return { rate, channels, samples };
}

/**
 * Encodes mono 16-bit samples as SPU2 ADPCM blocks, like adpenc: flag 6 on
 * the first block of a looping sample, 1 (or 3 to loop) on the last block of
 * data, then a silent block with flag 7.
 */
export function encodeAdpcm(samples, loop = false) {
    const blocks = Math.ceil(samples.length / BLOCK_SAMPLES);
    const out = Buffer.alloc((blocks + 1) * 16);
    const block = new Int16Array(BLOCK_SAMPLES);
    const predicted = new Float64Array(BLOCK_SAMPLES);
    const fourBit = new Int16Array(BLOCK_SAMPLES);
    // find_predict() and pack() keep their history across blocks.
    let findS1 = 0.0, findS2 = 0.0;
    let packS1 = 0.0, packS2 = 0.0;
    let predictNr = 0, shiftFactor = 0;
    let flags = 0;
    let loopState = loop ? 1 : 0;
    let left = samples.length;
    let written = 0;

    const findPredict = () => {
        const buffer = [];
        let min = 1e10;
        let s1 = 0.0, s2 = 0.0;
        for (let i = 0; i < 5; i++) {
            let max = 0.0;
            const column = new Float64Array(BLOCK_SAMPLES);
            s1 = findS1;
            s2 = findS2;
            for (let j = 0; j < BLOCK_SAMPLES; j++) {
                let s0 = block[j];
                if (s0 > 30719.0) s0 = 30719.0;
                if (s0 < -30720.0) s0 = -30720.0;
                const ds = s0 + s1 * FILTERS[i][0] + s2 * FILTERS[i][1];
                column[j] = ds;
                if (Math.abs(ds) > max) max = Math.abs(ds);
                s2 = s1;
                s1 = s0;
            }
            buffer[i] = column;
            if (max < min) {
                min = max;
                predictNr = i;
            }
            if (min <= 7) {
                predictNr = 0;
                break;
            }
        }
        findS1 = s1;
        findS2 = s2;
        for (let i = 0; i < BLOCK_SAMPLES; i++) predicted[i] = buffer[predictNr][i];

        const min2 = Math.trunc(min);
        let shiftMask = 0x4000;
        shiftFactor = 0;
        while (shiftFactor < 12) {
            if (shiftMask & (min2 + (shiftMask >> 3))) break;
            shiftFactor++;
            shiftMask >>= 1;
        }
    };

    const pack = () => {
        const filter = FILTERS[predictNr];
        for (let i = 0; i < BLOCK_SAMPLES; i++) {
            const s0 = predicted[i] + packS1 * filter[0] + packS2 * filter[1];
            const ds = s0 * (1 << shiftFactor);
            let di = (Math.trunc(ds) + 0x800) & 0xfffff000;
            if (di > 32767) di = 32767;
            if (di < -32768) di = -32768;
            fourBit[i] = di;
            di >>= shiftFactor;
            packS2 = packS1;
            packS1 = di - s0;
        }
    };

    for (let b = 0; b < blocks; b++) {
        block.fill(0);
        block.set(samples.subarray(b * BLOCK_SAMPLES, (b + 1) * BLOCK_SAMPLES));
        findPredict();
        pack();

        const at = written;
        out[at] = (predictNr << 4) | shiftFactor;
        if (loopState === 1) {
            out[at + 1] = 6;
            loopState = 2;
        } else {
            out[at + 1] = flags;
        }
        for (let k = 0; k < BLOCK_SAMPLES; k += 2)
            out[at + 2 + k / 2] = ((fourBit[k + 1] >> 8) & 0xf0) | ((fourBit[k] >> 12) & 0x0f);
        written += 16;

        left -= BLOCK_SAMPLES;
        if (left < BLOCK_SAMPLES) flags = loopState === 2 ? 3 : 1;
    }

    out[written] = (predictNr << 4) | shiftFactor;
    out[written + 1] = 7;
    return out;
}

/** WAV file contents to .adp file contents. */
export function encodeWav(wavBuffer, { loop = false } = {}) {
    const { rate, samples } = readWav(wavBuffer);
    const header = Buffer.alloc(16);
    header.write('APCM', 0, 'ascii');
    header[4] = 1; // version
    header[5] = 1; // channels: audsrv plays one voice
    header[6] = loop ? 1 : 0;
    header.writeUInt32LE(Math.floor((rate * 4096) / 48000), 8);
    header.writeUInt32LE(samples.length, 12);
    return Buffer.concat([header, encodeAdpcm(samples, loop)]);
}

function convertFile(input, output, loop) {
    const adp = encodeWav(fs.readFileSync(input), { loop });
    fs.writeFileSync(output, adp);
    console.log(`${input} -> ${output} (${adp.length - 16} bytes of SPU2 memory)`);
}

function convertDirectory(dir, loop) {
    let converted = 0;
    for (const entry of fs.readdirSync(dir, { withFileTypes: true, recursive: true })) {
        if (!entry.isFile() || path.extname(entry.name).toLowerCase() !== '.wav') continue;
        const input = path.join(entry.parentPath ?? entry.path, entry.name);
        const output = input.replace(/\.wav$/i, '.adp');
        if (fs.existsSync(output) && fs.statSync(output).mtimeMs >= fs.statSync(input).mtimeMs)
            continue;
        convertFile(input, output, loop);
        converted++;
    }
    console.log(`${converted} file(s) converted`);
}

function main(argv) {
    const args = argv.slice(2);
    let loop = false;
    let dir = null;
    const files = [];

    for (let i = 0; i < args.length; i++) {
        if (args[i] === '-L' || args[i] === '--loop') loop = true;
        else if (args[i] === '--dir') dir = args[++i];
        else if (args[i] === '-h' || args[i] === '--help') files.length = 0, dir = null, args.length = 0;
        else files.push(args[i]);
    }

    if (dir) {
        convertDirectory(dir, loop);
        return 0;
    }
    if (files.length < 1 || files.length > 2) {
        console.log('Usage: node tools/wav2adp.js [-L] input.wav [output.adp]\n' +
            '       node tools/wav2adp.js [-L] --dir <folder>\n' +
            '  -L, --loop  the sample loops forever');
        return 1;
    }
    convertFile(files[0], files[1] ?? files[0].replace(/\.wav$/i, '') + '.adp', loop);
    return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try {
        process.exitCode = main(process.argv);
    } catch (error) {
        console.error(`wav2adp: ${error.message}`);
        process.exitCode = 1;
    }
}
