#include <math.h>

#include <athena/noise.h>
#include <athena/random.h>

/* Output scales of Gustavson's simplex noise: about [-1, 1]. */
#define SIMPLEX2_SCALE 70.0f
#define SIMPLEX3_SCALE 32.0f

static const float grad3[12][3] = {
    { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
    { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
    { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 },
};

/* floorf without the libm call; coordinates stay within the int range. */
static inline int fast_floor(float x) {
    int i = (int)x;
    return x < (float)i ? i - 1 : i;
}

static inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

/* lowbias32 (Chris Wellons): full avalanche with two multiplies. */
static inline uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

void athena_noise_seed(AthenaNoise *noise, uint64_t seed) {
    AthenaRandom rng;

    athena_random_seed(&rng, seed);
    for (int i = 0; i < 256; i++)
        noise->perm[i] = (uint8_t)i;
    athena_random_shuffle(&rng, noise->perm, 256, 1);
    for (int i = 0; i < 256; i++) {
        noise->perm[i + 256] = noise->perm[i];
        noise->perm12[i] = noise->perm12[i + 256] = noise->perm[i] % 12;
    }
    noise->hash = athena_random_u32(&rng);
}

static inline float perlin_grad2(int hash, float x, float y) {
    switch (hash & 7) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}

static inline float perlin_grad3(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static inline float perlin2(const AthenaNoise *noise, float x, float y) {
    const uint8_t *p = noise->perm;
    int ix = fast_floor(x), iy = fast_floor(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float u = fade(fx), v = fade(fy);
    int a, b;

    ix &= 255;
    iy &= 255;
    a = p[ix] + iy;
    b = p[ix + 1] + iy;
    return lerp(
        lerp(perlin_grad2(p[a], fx, fy), perlin_grad2(p[b], fx - 1, fy), u),
        lerp(perlin_grad2(p[a + 1], fx, fy - 1),
            perlin_grad2(p[b + 1], fx - 1, fy - 1), u),
        v);
}

static inline float perlin3(const AthenaNoise *noise, float x, float y, float z) {
    const uint8_t *p = noise->perm;
    int ix = fast_floor(x), iy = fast_floor(y), iz = fast_floor(z);
    float fx = x - (float)ix, fy = y - (float)iy, fz = z - (float)iz;
    float u = fade(fx), v = fade(fy), w = fade(fz);
    int a, aa, ab, b, ba, bb;

    ix &= 255;
    iy &= 255;
    iz &= 255;
    a = p[ix] + iy;
    aa = p[a] + iz;
    ab = p[a + 1] + iz;
    b = p[ix + 1] + iy;
    ba = p[b] + iz;
    bb = p[b + 1] + iz;
    return lerp(
        lerp(lerp(perlin_grad3(p[aa], fx, fy, fz),
                perlin_grad3(p[ba], fx - 1, fy, fz), u),
            lerp(perlin_grad3(p[ab], fx, fy - 1, fz),
                perlin_grad3(p[bb], fx - 1, fy - 1, fz), u), v),
        lerp(lerp(perlin_grad3(p[aa + 1], fx, fy, fz - 1),
                perlin_grad3(p[ba + 1], fx - 1, fy, fz - 1), u),
            lerp(perlin_grad3(p[ab + 1], fx, fy - 1, fz - 1),
                perlin_grad3(p[bb + 1], fx - 1, fy - 1, fz - 1), u), v),
        w);
}

/* Contribution of one simplex corner (Gustavson). */
static inline float simplex_corner2(int gi, float x, float y) {
    float t = 0.5f - x * x - y * y;
    if (t < 0.0f)
        return 0.0f;
    t *= t;
    return t * t * (grad3[gi][0] * x + grad3[gi][1] * y);
}

static inline float simplex_corner3(int gi, float x, float y, float z) {
    float t = 0.6f - x * x - y * y - z * z;
    if (t < 0.0f)
        return 0.0f;
    t *= t;
    return t * t * (grad3[gi][0] * x + grad3[gi][1] * y + grad3[gi][2] * z);
}

static inline float simplex2(const AthenaNoise *noise, float x, float y) {
    const float F2 = 0.36602540378f;   /* (sqrt(3) - 1) / 2 */
    const float G2 = 0.21132486540f;   /* (3 - sqrt(3)) / 6 */
    const uint8_t *p = noise->perm, *p12 = noise->perm12;
    float s = (x + y) * F2;
    int i = fast_floor(x + s), j = fast_floor(y + s);
    float t = (float)(i + j) * G2;
    float x0 = x - ((float)i - t), y0 = y - ((float)j - t);
    int i1 = x0 > y0, j1 = !i1;
    float x1 = x0 - (float)i1 + G2, y1 = y0 - (float)j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2, y2 = y0 - 1.0f + 2.0f * G2;
    int ii = i & 255, jj = j & 255;

    return SIMPLEX2_SCALE * (
        simplex_corner2(p12[ii + p[jj]], x0, y0) +
        simplex_corner2(p12[ii + i1 + p[jj + j1]], x1, y1) +
        simplex_corner2(p12[ii + 1 + p[jj + 1]], x2, y2));
}

static inline float simplex3(const AthenaNoise *noise, float x, float y, float z) {
    const float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
    const uint8_t *p = noise->perm, *p12 = noise->perm12;
    float s = (x + y + z) * F3;
    int i = fast_floor(x + s), j = fast_floor(y + s), k = fast_floor(z + s);
    float t = (float)(i + j + k) * G3;
    float x0 = x - ((float)i - t), y0 = y - ((float)j - t), z0 = z - ((float)k - t);
    int i1, j1, k1, i2, j2, k2, ii, jj, kk;

    /* The simplex of the point: order of the offsets' magnitudes. */
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
        } else if (x0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1;
        } else {
            i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1;
        }
    } else {
        if (y0 < z0) {
            i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1;
        } else if (x0 < z0) {
            i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1;
        } else {
            i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
        }
    }
    ii = i & 255;
    jj = j & 255;
    kk = k & 255;
    return SIMPLEX3_SCALE * (
        simplex_corner3(p12[ii + p[jj + p[kk]]], x0, y0, z0) +
        simplex_corner3(p12[ii + i1 + p[jj + j1 + p[kk + k1]]],
            x0 - (float)i1 + G3, y0 - (float)j1 + G3, z0 - (float)k1 + G3) +
        simplex_corner3(p12[ii + i2 + p[jj + j2 + p[kk + k2]]],
            x0 - (float)i2 + 2.0f * G3, y0 - (float)j2 + 2.0f * G3,
            z0 - (float)k2 + 2.0f * G3) +
        simplex_corner3(p12[ii + 1 + p[jj + 1 + p[kk + 1]]],
            x0 - 1.0f + 3.0f * G3, y0 - 1.0f + 3.0f * G3, z0 - 1.0f + 3.0f * G3));
}

static inline uint32_t cell_hash(uint32_t seed, int x, int y, int z) {
    return mix32(seed ^ (uint32_t)x * 0x8DA6B343u ^ (uint32_t)y * 0xD8163841u ^
        (uint32_t)z * 0xCB1AB31Fu);
}

static inline float worley2(const AthenaNoise *noise, float x, float y) {
    int cx = fast_floor(x), cy = fast_floor(y);
    float fx = x - (float)cx, fy = y - (float)cy;
    float best = 8.0f;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            uint32_t h = cell_hash(noise->hash, cx + dx, cy + dy, 0);
            float px = (float)dx + (float)(h & 0xFFFF) * (1.0f / 65536.0f) - fx;
            float py = (float)dy + (float)(h >> 16) * (1.0f / 65536.0f) - fy;
            float d = px * px + py * py;
            if (d < best)
                best = d;
        }
    }
    return sqrtf(best);
}

static inline float worley3(const AthenaNoise *noise, float x, float y, float z) {
    int cx = fast_floor(x), cy = fast_floor(y), cz = fast_floor(z);
    float fx = x - (float)cx, fy = y - (float)cy, fz = z - (float)cz;
    float best = 12.0f;

    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                uint32_t h = cell_hash(noise->hash, cx + dx, cy + dy, cz + dz);
                float px = (float)dx + (float)(h & 0x3FF) * (1.0f / 1024.0f) - fx;
                float py = (float)dy + (float)((h >> 10) & 0x3FF) * (1.0f / 1024.0f) - fy;
                float pz = (float)dz + (float)(h >> 22) * (1.0f / 1024.0f) - fz;
                float d = px * px + py * py + pz * pz;
                if (d < best)
                    best = d;
            }
        }
    }
    return sqrtf(best);
}

/* Public entry points of the inline cores above. */
float athena_noise_perlin2(const AthenaNoise *noise, float x, float y) {
    return perlin2(noise, x, y);
}

float athena_noise_perlin3(const AthenaNoise *noise, float x, float y, float z) {
    return perlin3(noise, x, y, z);
}

float athena_noise_simplex2(const AthenaNoise *noise, float x, float y) {
    return simplex2(noise, x, y);
}

float athena_noise_simplex3(const AthenaNoise *noise, float x, float y, float z) {
    return simplex3(noise, x, y, z);
}

float athena_noise_worley2(const AthenaNoise *noise, float x, float y) {
    return worley2(noise, x, y);
}

float athena_noise_worley3(const AthenaNoise *noise, float x, float y, float z) {
    return worley3(noise, x, y, z);
}

static inline float sample2(const AthenaNoise *noise, AthenaNoiseType type,
    float x, float y) {
    switch (type) {
    case ATHENA_NOISE_PERLIN: return perlin2(noise, x, y);
    case ATHENA_NOISE_WORLEY: return worley2(noise, x, y);
    default: return simplex2(noise, x, y);
    }
}

static inline float sample3(const AthenaNoise *noise, AthenaNoiseType type,
    float x, float y, float z) {
    switch (type) {
    case ATHENA_NOISE_PERLIN: return perlin3(noise, x, y, z);
    case ATHENA_NOISE_WORLEY: return worley3(noise, x, y, z);
    default: return simplex3(noise, x, y, z);
    }
}

float athena_noise_sample2(const AthenaNoise *noise, AthenaNoiseType type,
    float x, float y) {
    return sample2(noise, type, x, y);
}

float athena_noise_sample3(const AthenaNoise *noise, AthenaNoiseType type,
    float x, float y, float z) {
    return sample3(noise, type, x, y, z);
}

void athena_noise_fractal_default(AthenaNoiseFractal *fractal) {
    fractal->type = ATHENA_NOISE_SIMPLEX;
    fractal->mode = ATHENA_NOISE_FBM;
    fractal->octaves = 4;
    fractal->lacunarity = 2.0f;
    fractal->gain = 0.5f;
    fractal->warp = 0.0f;
}

/*
 * Frequencies and amplitudes of the octaves, the amplitudes already divided
 * by the sum of their magnitudes: computed once per sum, or once per fill.
 */
typedef struct {
    int count;
    float frequency[ATHENA_NOISE_MAX_OCTAVES];
    float amplitude[ATHENA_NOISE_MAX_OCTAVES];
} Octaves;

static void octaves_setup(Octaves *octaves, const AthenaNoiseFractal *fractal) {
    float frequency = 1.0f, amplitude = 1.0f, total = 0.0f;
    int count = fractal->octaves;

    count = count < 1 ? 1 : count > ATHENA_NOISE_MAX_OCTAVES ?
        ATHENA_NOISE_MAX_OCTAVES : count;
    for (int i = 0; i < count; i++) {
        octaves->frequency[i] = frequency;
        octaves->amplitude[i] = amplitude;
        total += fabsf(amplitude);
        frequency *= fractal->lacunarity;
        amplitude *= fractal->gain;
    }
    /* total >= 1: the first amplitude is 1. */
    for (int i = 0; i < count; i++)
        octaves->amplitude[i] /= total;
    octaves->count = count;
}

static inline float shape(AthenaNoiseMode mode, float n) {
    switch (mode) {
    case ATHENA_NOISE_RIDGED: {
        float r = 1.0f - fabsf(n);
        return 2.0f * r * r - 1.0f;
    }
    case ATHENA_NOISE_BILLOW:
        return 2.0f * fabsf(n) - 1.0f;
    default:
        return n;
    }
}

/*
 * Domain warp: moves the point by `warp` times simplex noise sampled at
 * offset points, so each axis moves independently. Simplex for every type:
 * it is signed, smooth and cheap.
 */
static inline void warp2(const AthenaNoise *noise, float warp, float *x,
    float *y) {
    float dx = simplex2(noise, *x + 5.2f, *y + 1.3f);
    float dy = simplex2(noise, *x - 7.9f, *y + 4.6f);

    *x += warp * dx;
    *y += warp * dy;
}

static inline void warp3(const AthenaNoise *noise, float warp, float *x,
    float *y, float *z) {
    float dx = simplex3(noise, *x + 5.2f, *y + 1.3f, *z - 3.1f);
    float dy = simplex3(noise, *x - 7.9f, *y + 4.6f, *z + 2.8f);
    float dz = simplex3(noise, *x + 3.7f, *y - 6.4f, *z + 8.3f);

    *x += warp * dx;
    *y += warp * dy;
    *z += warp * dz;
}

float athena_noise_fbm2(const AthenaNoise *noise,
    const AthenaNoiseFractal *fractal, float x, float y) {
    Octaves octaves;
    float sum = 0.0f;

    octaves_setup(&octaves, fractal);
    if (fractal->warp != 0.0f)
        warp2(noise, fractal->warp, &x, &y);
    for (int i = 0; i < octaves.count; i++) {
        float f = octaves.frequency[i];
        sum += octaves.amplitude[i] *
            shape(fractal->mode, sample2(noise, fractal->type, x * f, y * f));
    }
    return sum;
}

float athena_noise_fbm3(const AthenaNoise *noise,
    const AthenaNoiseFractal *fractal, float x, float y, float z) {
    Octaves octaves;
    float sum = 0.0f;

    octaves_setup(&octaves, fractal);
    if (fractal->warp != 0.0f)
        warp3(noise, fractal->warp, &x, &y, &z);
    for (int i = 0; i < octaves.count; i++) {
        float f = octaves.frequency[i];
        sum += octaves.amplitude[i] * shape(fractal->mode,
            sample3(noise, fractal->type, x * f, y * f, z * f));
    }
    return sum;
}

/*
 * Grid loops, one per noise and dimension, so the noise is inlined in its
 * loop: no type switch, octave setup or division per cell. They write the
 * raw sums of `rows` rows from `first` at `out` (the start of that first
 * row); athena_noise_fill_finish() maps them onto the output range.
 */
#define DEFINE_FILL2(name, SAMPLE) \
static void name(const AthenaNoise *noise, const AthenaNoiseFill *fill, \
    const Octaves *octaves, float *out, size_t width, size_t first, \
    size_t rows) { \
    AthenaNoiseMode mode = fill->fractal.mode; \
    float warp = fill->fractal.warp; \
    for (size_t row = first; row < first + rows; row++) { \
        float row_y = ((float)row + fill->y) * fill->scale; \
        for (size_t col = 0; col < width; col++) { \
            float x = ((float)col + fill->x) * fill->scale, y = row_y; \
            float sum = 0.0f; \
            if (warp != 0.0f) \
                warp2(noise, warp, &x, &y); \
            for (int i = 0; i < octaves->count; i++) { \
                float f = octaves->frequency[i]; \
                sum += octaves->amplitude[i] * \
                    shape(mode, SAMPLE(noise, x * f, y * f)); \
            } \
            *out++ = sum; \
        } \
    } \
}

#define DEFINE_FILL3(name, SAMPLE) \
static void name(const AthenaNoise *noise, const AthenaNoiseFill *fill, \
    const Octaves *octaves, float *out, size_t width, size_t first, \
    size_t rows) { \
    AthenaNoiseMode mode = fill->fractal.mode; \
    float warp = fill->fractal.warp; \
    float slice = fill->z * fill->scale; \
    for (size_t row = first; row < first + rows; row++) { \
        float row_y = ((float)row + fill->y) * fill->scale; \
        for (size_t col = 0; col < width; col++) { \
            float x = ((float)col + fill->x) * fill->scale, y = row_y; \
            float z = slice, sum = 0.0f; \
            if (warp != 0.0f) \
                warp3(noise, warp, &x, &y, &z); \
            for (int i = 0; i < octaves->count; i++) { \
                float f = octaves->frequency[i]; \
                sum += octaves->amplitude[i] * \
                    shape(mode, SAMPLE(noise, x * f, y * f, z * f)); \
            } \
            *out++ = sum; \
        } \
    } \
}

DEFINE_FILL2(fill_perlin2, perlin2)
DEFINE_FILL2(fill_simplex2, simplex2)
DEFINE_FILL2(fill_worley2, worley2)
DEFINE_FILL3(fill_perlin3, perlin3)
DEFINE_FILL3(fill_simplex3, simplex3)
DEFINE_FILL3(fill_worley3, worley3)

typedef void FillLoop(const AthenaNoise *, const AthenaNoiseFill *,
    const Octaves *, float *, size_t, size_t, size_t);

void athena_noise_fill_rows(const AthenaNoise *noise,
    const AthenaNoiseFill *fill, float *out, size_t width, size_t first,
    size_t rows) {
    Octaves octaves;
    FillLoop *loop;

    if (!width || !rows)
        return;
    octaves_setup(&octaves, &fill->fractal);
    switch (fill->fractal.type) {
    case ATHENA_NOISE_PERLIN:
        loop = fill->use_z ? fill_perlin3 : fill_perlin2;
        break;
    case ATHENA_NOISE_WORLEY:
        loop = fill->use_z ? fill_worley3 : fill_worley2;
        break;
    default:
        loop = fill->use_z ? fill_simplex3 : fill_simplex2;
        break;
    }
    loop(noise, fill, &octaves, out + first * width, width, first, rows);
}

void athena_noise_fill(const AthenaNoise *noise, const AthenaNoiseFill *fill,
    float *out, size_t width, size_t height) {
    athena_noise_fill_rows(noise, fill, out, width, 0, height);
    athena_noise_fill_finish(fill, out, width * height);
}

void athena_noise_fill_finish(const AthenaNoiseFill *fill, float *out,
    size_t count) {
    float low, high, scale, span = fill->max - fill->min;

    if (!count)
        return;
    /* The range mapped onto [min, max]: nominal, or the one produced. */
    if (fill->normalize) {
        low = high = out[0];
        for (size_t i = 1; i < count; i++) {
            low = out[i] < low ? out[i] : low;
            high = out[i] > high ? out[i] : high;
        }
    } else {
        low = fill->fractal.type == ATHENA_NOISE_WORLEY ? 0.0f : -1.0f;
        high = 1.0f;
    }
    scale = high > low ? 1.0f / (high - low) : 0.0f;
    for (size_t i = 0; i < count; i++) {
        float t = (out[i] - low) * scale;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        out[i] = fill->min + t * span;
    }
}

void athena_noise_to_tiles(uint16_t *out, const float *values, size_t count,
    const float *thresholds, const uint16_t *tiles, size_t levels) {
    for (size_t i = 0; i < count; i++) {
        size_t k = 0;
        while (k < levels && values[i] >= thresholds[k])
            k++;
        out[i] = tiles[k];
    }
}
