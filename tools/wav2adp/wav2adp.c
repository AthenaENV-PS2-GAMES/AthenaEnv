/*
 * wav2adp: converts WAV files to the .adp sound effects Sound.Sfx loads
 * (SPU2 ADPCM with an APCM header). C port of tools/wav2adp.js for builds
 * without Node: `make adp ADP_DIR=...` compiles it with the host compiler.
 *
 * Same output as tools/wav2adp.js (tests/host/run.sh compares them) and,
 * for 16-bit mono input, as the PS2SDK's adpenc. 8/16/24/32-bit PCM and
 * 32-bit float are accepted; stereo is mixed down to mono, since audsrv
 * plays one voice per sample.
 *
 * Usage: wav2adp [-L] input.wav output.adp     (-L: the sample loops)
 *
 * Plain C99 and libm; builds with any host compiler.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SAMPLES 28
/* audsrv voices: pitch register 0x1000 = 48 kHz, 14 bits. */
#define MAX_PITCH 0x3fff

static const double filters[5][2] = {
    { 0.0, 0.0 },
    { -60.0 / 64.0, 0.0 },
    { -115.0 / 64.0, 52.0 / 64.0 },
    { -98.0 / 64.0, 55.0 / 64.0 },
    { -122.0 / 64.0, 60.0 / 64.0 },
};

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int16_t s16(const uint8_t *p) {
    return (int16_t)le16(p);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int fail(const char *message, const char *detail) {
    fprintf(stderr, "wav2adp: %s%s%s\n", message, detail ? ": " : "", detail ? detail : "");
    return 1;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    long length;

    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

typedef struct {
    uint32_t rate;
    int16_t *samples;
    uint32_t count;
} Wav;

/* Parses a RIFF/WAVE file into mono 16-bit samples (like readWav() in wav2adp.js). */
static const char *parse_wav(const uint8_t *buf, size_t size, Wav *wav) {
    const uint8_t *format = NULL, *data = NULL;
    size_t data_size = 0, offset;
    uint32_t format_size = 0;
    unsigned tag, channels, bits, sample_bytes, frame_bytes;
    uint32_t frames, pitch;

    if (size < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4))
        return "not a WAV file";
    for (offset = 12; offset + 8 <= size;) {
        uint32_t chunk = le32(buf + offset + 4);
        size_t body = offset + 8;
        if (!memcmp(buf + offset, "fmt ", 4)) {
            format = buf + body;
            format_size = chunk;
            if (format_size < 16 || body + 16 > size)
                return "short \"fmt \" chunk";
        } else if (!memcmp(buf + offset, "data", 4)) {
            /* Streamed writers leave 0 or 0xFFFFFFFF: the data runs to the end. */
            data = buf + body;
            data_size = chunk == 0 || body + chunk > size ? size - body : chunk;
            break;
        }
        offset = body + chunk + (chunk & 1);
    }
    if (!format)
        return "no \"fmt \" chunk";
    if (!data)
        return "no \"data\" chunk";

    tag = le16(format);
    /* WAVE_FORMAT_EXTENSIBLE: the real tag opens the sub-format GUID. */
    if (tag == 0xfffe && format_size >= 26)
        tag = le16(format + 24);
    channels = le16(format + 2);
    wav->rate = le32(format + 4);
    bits = le16(format + 14);
    if (!((tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) ||
          (tag == 3 && bits == 32)))
        return "only PCM (8/16/24/32-bit) and 32-bit float WAV files are supported";
    if (channels < 1)
        return "no channels";
    pitch = (uint32_t)(((uint64_t)wav->rate * 4096) / 48000);
    if (pitch < 1 || pitch > MAX_PITCH)
        return "sample rate out of range for the SPU2 (12..191999 Hz)";

    sample_bytes = bits / 8;
    frame_bytes = sample_bytes * channels;
    frames = (uint32_t)(data_size / frame_bytes);
    wav->samples = malloc(sizeof(int16_t) * (frames ? frames : 1));
    if (!wav->samples)
        return "out of memory";
    for (uint32_t i = 0; i < frames; i++) {
        double sum = 0;
        for (unsigned c = 0; c < channels; c++) {
            const uint8_t *p = data + (size_t)i * frame_bytes + c * sample_bytes;
            int value;
            if (tag == 3) {
                float f;
                uint32_t raw = le32(p);
                memcpy(&f, &raw, sizeof(f));
                double d = f;
                if (!(d > -1))
                    value = d <= -1 ? -32768 : 0;
                else
                    value = d >= 1 ? 32767 : (int)trunc(d * 32767);
            } else if (bits == 8) {
                value = (p[0] - 128) * 256;
            } else if (bits == 16) {
                value = s16(p);
            } else if (bits == 24) {
                value = s16(p + 1);
            } else {
                value = s16(p + 2);
            }
            sum += value;
        }
        /* Math.round(): halves round up. */
        wav->samples[i] = (int16_t)floor(sum / channels + 0.5);
    }
    wav->count = frames;
    return NULL;
}

/* Encoder state; find_predict() and pack() keep their history across blocks, like adpenc. */
typedef struct {
    double find_s1, find_s2, pack_s1, pack_s2;
    int predict, shift;
} Encoder;

static void find_predict(Encoder *e, const int16_t *block, double *predicted) {
    double buffer[5][BLOCK_SAMPLES];
    double min = 1e10, s1 = 0.0, s2 = 0.0;
    int min2, mask;

    for (int i = 0; i < 5; i++) {
        double max = 0.0;
        s1 = e->find_s1;
        s2 = e->find_s2;
        for (int j = 0; j < BLOCK_SAMPLES; j++) {
            double s0 = block[j];
            if (s0 > 30719.0) s0 = 30719.0;
            if (s0 < -30720.0) s0 = -30720.0;
            double ds = s0 + s1 * filters[i][0] + s2 * filters[i][1];
            buffer[i][j] = ds;
            if (fabs(ds) > max) max = fabs(ds);
            s2 = s1;
            s1 = s0;
        }
        if (max < min) {
            min = max;
            e->predict = i;
        }
        if (min <= 7) {
            e->predict = 0;
            break;
        }
    }
    e->find_s1 = s1;
    e->find_s2 = s2;
    for (int i = 0; i < BLOCK_SAMPLES; i++)
        predicted[i] = buffer[e->predict][i];

    min2 = (int)min;
    mask = 0x4000;
    e->shift = 0;
    while (e->shift < 12) {
        if (mask & (min2 + (mask >> 3)))
            break;
        e->shift++;
        mask >>= 1;
    }
}

static void pack(Encoder *e, const double *predicted, int16_t *four_bit) {
    const double *filter = filters[e->predict];

    for (int i = 0; i < BLOCK_SAMPLES; i++) {
        double s0 = predicted[i] + e->pack_s1 * filter[0] + e->pack_s2 * filter[1];
        double ds = s0 * (double)(1 << e->shift);
        int32_t di = (int32_t)(((uint32_t)(int32_t)ds + 0x800) & 0xfffff000u);
        if (di > 32767) di = 32767;
        if (di < -32768) di = -32768;
        four_bit[i] = (int16_t)di;
        /* Arithmetic shift, as adpenc's portable version. */
        di = di < 0 ? ~(~di >> e->shift) : di >> e->shift;
        e->pack_s2 = e->pack_s1;
        e->pack_s1 = (double)di - s0;
    }
}

/* SPU2 ADPCM blocks, like adpenc: flag 6 on the first block of a loop, 1 (3
 * to loop) on the last block of data, then a silent block with flag 7. */
static uint8_t *encode(const int16_t *samples, uint32_t count, int loop, size_t *size) {
    uint32_t blocks = (count + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
    uint8_t *out = calloc((size_t)(blocks + 1) * 16, 1);
    Encoder e = { 0 };
    int loop_state = loop ? 1 : 0, flags = 0;
    long left = (long)count;
    size_t at = 0;

    if (!out)
        return NULL;
    for (uint32_t b = 0; b < blocks; b++) {
        int16_t block[BLOCK_SAMPLES] = { 0 }, four_bit[BLOCK_SAMPLES];
        double predicted[BLOCK_SAMPLES];
        uint32_t n = count - b * BLOCK_SAMPLES;

        memcpy(block, samples + b * BLOCK_SAMPLES,
            sizeof(int16_t) * (n < BLOCK_SAMPLES ? n : BLOCK_SAMPLES));
        find_predict(&e, block, predicted);
        pack(&e, predicted, four_bit);

        out[at] = (uint8_t)((e.predict << 4) | e.shift);
        if (loop_state == 1) {
            out[at + 1] = 6;
            loop_state = 2;
        } else {
            out[at + 1] = (uint8_t)flags;
        }
        for (int k = 0; k < BLOCK_SAMPLES; k += 2)
            out[at + 2 + k / 2] = (uint8_t)(((four_bit[k + 1] >> 8) & 0xf0) | ((four_bit[k] >> 12) & 0x0f));
        at += 16;

        left -= BLOCK_SAMPLES;
        if (left < BLOCK_SAMPLES)
            flags = loop_state == 2 ? 3 : 1;
    }
    out[at] = (uint8_t)((e.predict << 4) | e.shift);
    out[at + 1] = 7;
    *size = at + 16;
    return out;
}

int main(int argc, char **argv) {
    const char *input = NULL, *output = NULL, *error;
    uint8_t header[16] = { 'A', 'P', 'C', 'M', 1, 1 };
    uint8_t *file, *blocks;
    size_t size, blocks_size;
    int loop = 0;
    Wav wav = { 0 };
    FILE *out;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-L") || !strcmp(argv[i], "--loop")) loop = 1;
        else if (!input) input = argv[i];
        else if (!output) output = argv[i];
        else input = NULL, i = argc;
    }
    if (!input || !output) {
        fprintf(stderr, "Usage: wav2adp [-L] input.wav output.adp\n  -L, --loop  the sample loops forever\n");
        return 1;
    }
    file = read_file(input, &size);
    if (!file)
        return fail("cannot read", input);
    error = parse_wav(file, size, &wav);
    free(file);
    if (error)
        return fail(error, input);
    blocks = encode(wav.samples, wav.count, loop, &blocks_size);
    free(wav.samples);
    if (!blocks)
        return fail("out of memory", NULL);

    header[6] = (uint8_t)loop;
    put32(header + 8, (uint32_t)(((uint64_t)wav.rate * 4096) / 48000));
    put32(header + 12, wav.count);
    out = fopen(output, "wb");
    if (!out || fwrite(header, 1, 16, out) != 16 || fwrite(blocks, 1, blocks_size, out) != blocks_size) {
        if (out) fclose(out);
        free(blocks);
        return fail("cannot write", output);
    }
    fclose(out);
    free(blocks);
    printf("%s -> %s (%zu bytes of SPU2 memory)\n", input, output, blocks_size);
    return 0;
}
