#ifndef ATHENA_NOISE_H
#define ATHENA_NOISE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Coherent noise for procedural generation, computed in float (the EE's FPU
 * is single precision): Perlin and simplex in 2D and 3D, Worley (cellular)
 * and fractal sums (fBm) of any of them, plus batch fills of whole grids.
 *
 *     AthenaNoise noise;
 *     athena_noise_seed(&noise, 7);
 *     float h = athena_noise_simplex2(&noise, x * 0.05f, y * 0.05f);
 *
 * Coordinates must be finite and well inside the int range: the lattice cell
 * is the integer part of each coordinate.
 */

typedef struct {
    uint8_t perm[512];      /* permutation of 0..255, repeated */
    uint8_t perm12[512];    /* perm[i] % 12, simplex gradient indices */
    uint32_t hash;          /* Worley feature-point seed */
} AthenaNoise;

typedef enum {
    ATHENA_NOISE_PERLIN,
    ATHENA_NOISE_SIMPLEX,
    ATHENA_NOISE_WORLEY,
} AthenaNoiseType;

/*
 * How each octave is shaped before the sum. Ridged and billow need a signed
 * noise (Perlin or simplex); all three stay in about [-1, 1].
 */
typedef enum {
    ATHENA_NOISE_FBM,       /* the noise itself */
    ATHENA_NOISE_RIDGED,    /* 2 * (1 - |n|)^2 - 1: sharp crests, mountains */
    ATHENA_NOISE_BILLOW,    /* 2 * |n| - 1: rounded bumps, clouds, dunes */
} AthenaNoiseMode;

/* Octaves of a fractal sum: each one `lacunarity` times the frequency. */
typedef struct {
    AthenaNoiseType type;
    AthenaNoiseMode mode;
    int octaves;            /* 1 to ATHENA_NOISE_MAX_OCTAVES */
    float lacunarity;       /* frequency multiplier per octave, usually 2 */
    float gain;             /* amplitude multiplier per octave, usually 0.5 */
    /*
     * Domain warp, in noise units: before the octaves, the point moves by
     * `warp` times simplex noise sampled at offset points. 0 is off.
     * Twists straight features into organic ones (coasts, rivers, marble).
     */
    float warp;
} AthenaNoiseFractal;

#define ATHENA_NOISE_MAX_OCTAVES 16

/* A grid sampled by athena_noise_fill(). */
typedef struct {
    AthenaNoiseFractal fractal;
    float scale;            /* noise units per cell */
    float x, y;             /* offset in cells: chunks at x = n * width tile */
    float z;                /* 3D slice in cells, when use_z is set */
    int use_z;
    float min, max;         /* output range */
    /*
     * Stretches the values actually produced onto [min, max] instead of the
     * nominal range of the noise. Chunks filled separately no longer match.
     */
    int normalize;
} AthenaNoiseFill;

/* Default fractal: simplex fBm, 4 octaves, lacunarity 2, gain 0.5, no warp. */
void athena_noise_fractal_default(AthenaNoiseFractal *fractal);

/* Builds the tables of a seed (shuffled with <athena/random.h>). Any seed is valid. */
void athena_noise_seed(AthenaNoise *noise, uint64_t seed);

/* Perlin ("improved noise") and simplex noise: about [-1, 1]. */
float athena_noise_perlin2(const AthenaNoise *noise, float x, float y);
float athena_noise_perlin3(const AthenaNoise *noise, float x, float y, float z);
float athena_noise_simplex2(const AthenaNoise *noise, float x, float y);
float athena_noise_simplex3(const AthenaNoise *noise, float x, float y, float z);

/*
 * Worley (F1): distance to the nearest feature point, one jittered point per
 * cell. [0, about 1.2]; athena_noise_fill() clamps it to [0, 1].
 */
float athena_noise_worley2(const AthenaNoise *noise, float x, float y);
float athena_noise_worley3(const AthenaNoise *noise, float x, float y, float z);

/* One octave of `type`. */
float athena_noise_sample2(const AthenaNoise *noise, AthenaNoiseType type,
    float x, float y);
float athena_noise_sample3(const AthenaNoise *noise, AthenaNoiseType type,
    float x, float y, float z);

/*
 * Fractal sum (fBm, ridged or billow, optionally warped), divided by the sum
 * of the amplitudes: stays in the range of `type`.
 */
float athena_noise_fbm2(const AthenaNoise *noise,
    const AthenaNoiseFractal *fractal, float x, float y);
float athena_noise_fbm3(const AthenaNoise *noise,
    const AthenaNoiseFractal *fractal, float x, float y, float z);

/*
 * Fills `width * height` floats, row by row. Cell (col, row) samples
 * ((col + x) * scale, (row + y) * scale[, z * scale]) with the fractal. The
 * noise range (about [-1, 1], or [0, 1] for Worley) is mapped onto
 * [min, max] and clamped to it, unless `normalize` stretches the actual one.
 */
void athena_noise_fill(const AthenaNoise *noise, const AthenaNoiseFill *fill,
    float *out, size_t width, size_t height);

/*
 * athena_noise_fill() in steps, for work spread over time: the raw fractal
 * sums of `rows` rows from row `first` (written at out + first * width), then
 * athena_noise_fill_finish() once every row is done, to map the whole grid
 * of `count` values onto [min, max].
 */
void athena_noise_fill_rows(const AthenaNoise *noise,
    const AthenaNoiseFill *fill, float *out, size_t width, size_t first,
    size_t rows);
void athena_noise_fill_finish(const AthenaNoiseFill *fill, float *out,
    size_t count);

/*
 * Background fill (<athena/job.h>): a worker computes the grid into a buffer
 * of its own, in slices of rows, checking for cancellation between them.
 * The job copies `noise` and `fill`; the caller's may change right away.
 * NULL when it cannot start (no memory, no worker).
 */
struct AthenaJob;
struct AthenaJob *athena_noise_fill_submit(const AthenaNoise *noise,
    const AthenaNoiseFill *fill, size_t width, size_t height);

/* Rows computed so far, for progress; any thread. */
size_t athena_noise_fill_job_rows(struct AthenaJob *job);

/*
 * Once the job is DONE: its buffer of width * height floats, now owned by
 * the caller (free() it), or NULL if already taken.
 */
float *athena_noise_fill_job_take(struct AthenaJob *job);

/* What a cancelled fill job returns (athena_job_state result). */
#define ATHENA_NOISE_JOB_CANCELLED (-2)

/*
 * Classifies values into tiles: out[i] = tiles[k], where k is how many of the
 * `levels` ascending thresholds are <= values[i]. `tiles` has levels + 1
 * entries.
 */
void athena_noise_to_tiles(uint16_t *out, const float *values, size_t count,
    const float *thresholds, const uint16_t *tiles, size_t levels);

#endif /* ATHENA_NOISE_H */
