/* Host test of Random (xoshiro128**, distributions) and Noise (ranges, determinism, continuity, fill, toTiles). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/noise.h>
#include <athena/random.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_reference(void) {
    /* Reference outputs of xoshiro128** (prng.di.unimi.it) from {1, 2, 3, 4}. */
    static const uint32_t state[4] = { 1, 2, 3, 4 };
    static const uint32_t expected[6] = { 11520, 0, 5927040, 70819200, 2031721883, 1637235492 };
    static const uint32_t zero[4] = { 0, 0, 0, 0 };
    AthenaRandom rng;

    CHECK(athena_random_set_state(&rng, state), "set_state accepts a non-zero state");
    for (int i = 0; i < 6; i++) {
        uint32_t value = athena_random_u32(&rng);
        CHECK(value == expected[i], "output %d: %u, expected %u", i, value, expected[i]);
    }
    CHECK(!athena_random_set_state(&rng, zero), "all-zero state rejected");

    /* splitmix64 from 0: 0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4. */
    athena_random_seed(&rng, 0);
    CHECK(rng.s[0] == 0x7B1DCDAFu && rng.s[1] == 0xE220A839u &&
        rng.s[2] == 0xA1B965F4u && rng.s[3] == 0x6E789E6Au,
        "seed 0 state %08x %08x %08x %08x", rng.s[0], rng.s[1], rng.s[2], rng.s[3]);

    CHECK(athena_random_hash("", 0) == 0xCBF29CE484222325ULL, "FNV-1a of nothing");
    CHECK(athena_random_hash("a", 1) == 0xAF63DC4C8601EC8CULL, "FNV-1a of 'a'");
}

static void test_determinism(void) {
    AthenaRandom a, b, c;
    int same = 1, differ = 0;

    athena_random_seed(&a, 1234);
    athena_random_seed(&b, 1234);
    athena_random_seed(&c, 1235);
    for (int i = 0; i < 1000; i++) {
        uint32_t va = athena_random_u32(&a), vc = athena_random_u32(&c);
        same &= va == athena_random_u32(&b);
        differ += va != vc;
    }
    CHECK(same, "same seed, same sequence");
    CHECK(differ > 990, "neighbour seeds differ (%d of 1000)", differ);

    /* A copied state continues the same sequence. */
    b = a;
    CHECK(athena_random_u32(&a) == athena_random_u32(&b), "copied state continues");
}

static void test_int(void) {
    AthenaRandom rng;
    int counts[6] = { 0 };
    int in_range = 1, swapped = 1, full_ok = 1;
    const int n = 60000;

    athena_random_seed(&rng, 42);
    for (int i = 0; i < n; i++) {
        int32_t v = athena_random_int(&rng, 1, 6);
        if (v < 1 || v > 6) {
            in_range = 0;
            continue;
        }
        counts[v - 1]++;
    }
    CHECK(in_range, "int(1, 6) stays in range");
    for (int i = 0; i < 6; i++)
        CHECK(counts[i] > n / 6 - 500 && counts[i] < n / 6 + 500, "face %d: %d", i + 1, counts[i]);

    for (int i = 0; i < 1000; i++) {
        int32_t v = athena_random_int(&rng, 10, -10);
        swapped &= v >= -10 && v <= 10;
    }
    CHECK(swapped, "reversed bounds are swapped");
    CHECK(athena_random_int(&rng, 7, 7) == 7, "single-value range");
    for (int i = 0; i < 100; i++)
        (void)athena_random_int(&rng, INT32_MIN, INT32_MAX);   /* full range: no UB */
    for (int i = 0; i < 1000; i++) {
        int32_t v = athena_random_int(&rng, INT32_MAX - 1, INT32_MAX);
        full_ok &= v >= INT32_MAX - 1;
    }
    CHECK(full_ok, "range at the int32 edge");
    CHECK(athena_random_below(&rng, 0) == 0, "below(0) is 0");
    CHECK(athena_random_below(&rng, 1) == 0, "below(1) is 0");
}

static void test_floats(void) {
    AthenaRandom rng;
    double sum = 0, sum2 = 0;
    int in_range = 1, range_ok = 1, trues = 0;
    const int n = 100000;

    athena_random_seed(&rng, 7);
    for (int i = 0; i < n; i++) {
        float v = athena_random_float(&rng);
        in_range &= v >= 0.0f && v < 1.0f;
        sum += v;
        float r = athena_random_range(&rng, -2.0f, 3.0f);
        range_ok &= r >= -2.0f && r < 3.0f;
        trues += athena_random_bool(&rng, 0.25f);
    }
    CHECK(in_range, "float() in [0, 1)");
    CHECK(fabs(sum / n - 0.5) < 0.01, "float() mean %f", sum / n);
    CHECK(range_ok, "range(-2, 3) in [-2, 3)");
    CHECK(athena_random_range(&rng, 5.0f, 5.0f) == 5.0f, "empty range gives min");
    CHECK(athena_random_range(&rng, 1.0f, 1.0000001f) < 1.0000001f, "tiny range stays below max");
    CHECK(abs(trues - n / 4) < 1000, "bool(0.25): %d of %d", trues, n);
    CHECK(!athena_random_bool(&rng, 0.0f) && athena_random_bool(&rng, 1.0f), "bool edges");
    CHECK(!athena_random_bool(&rng, NAN), "bool(NaN) is false");
    CHECK(athena_random_range(&rng, 0.0f, INFINITY) == 0.0f, "range to infinity gives min");
    CHECK(isnan(athena_random_range(&rng, NAN, 1.0f)), "range from NaN gives min");

    sum = 0;
    for (int i = 0; i < n; i++) {
        float g = athena_random_gaussian(&rng, 10.0f, 2.0f);
        sum += g;
        sum2 += (double)g * g;
    }
    double mean = sum / n, stddev = sqrt(sum2 / n - mean * mean);
    CHECK(fabs(mean - 10.0) < 0.05 && fabs(stddev - 2.0) < 0.05, "gaussian mean %f stddev %f", mean, stddev);
}

static void test_weighted_shuffle(void) {
    AthenaRandom rng;
    const float weights[4] = { 70, 0, 25, 5 };
    const float zeros[2] = { 0, 0 };
    const float negative[2] = { 1, -1 };
    const float nan_weight[2] = { 1, NAN };
    const float inf_weight[2] = { 1, INFINITY };
    int counts[4] = { 0 };
    const int n = 100000;

    athena_random_seed(&rng, 99);
    for (int i = 0; i < n; i++) {
        int k = athena_random_weighted(&rng, weights, 4);
        if (k >= 0 && k < 4)
            counts[k]++;
    }
    CHECK(counts[1] == 0, "zero weight never drawn");
    CHECK(abs(counts[0] - 70000) < 1000 && abs(counts[2] - 25000) < 1000 && abs(counts[3] - 5000) < 500,
        "weighted 70/0/25/5: %d %d %d %d", counts[0], counts[1], counts[2], counts[3]);
    CHECK(athena_random_weighted(&rng, zeros, 2) == -1, "all-zero weights");
    CHECK(athena_random_weighted(&rng, negative, 2) == -1, "negative weight");
    CHECK(athena_random_weighted(&rng, nan_weight, 2) == -1, "NaN weight");
    CHECK(athena_random_weighted(&rng, inf_weight, 2) == -1, "infinite weight");
    CHECK(athena_random_weighted(&rng, weights, 0) == -1, "no weights");

    /* Shuffle is a permutation, and every position sees every value. */
    int seen[8][8] = { { 0 } };
    for (int round = 0; round < 4000; round++) {
        uint16_t items[8];
        int present = 0;
        for (int i = 0; i < 8; i++)
            items[i] = (uint16_t)i;
        athena_random_shuffle(&rng, items, 8, sizeof(items[0]));
        for (int i = 0; i < 8; i++) {
            present |= 1 << items[i];
            seen[i][items[i]]++;
        }
        if (present != 0xFF) {
            CHECK(0, "shuffle lost an element");
            break;
        }
    }
    for (int i = 0; i < 8; i++)
        for (int v = 0; v < 8; v++)
            CHECK(seen[i][v] > 350 && seen[i][v] < 650, "position %d value %d: %d", i, v, seen[i][v]);
    athena_random_shuffle(&rng, NULL, 0, 4);   /* empty: no access */
}

static void test_batches_and_samples(void) {
    AthenaRandom rng;
    static float values[20001];
    int ok = 1;

    athena_random_seed(&rng, 31);
    for (int i = 0; i < 10000; i++) {
        float a = athena_random_angle(&rng);
        ok &= a >= 0.0f && a < 6.2831853f;
    }
    CHECK(ok, "angle in [0, 2 pi)");

    athena_random_fill_range(&rng, values, 20001, -3.0f, 5.0f);
    ok = 1;
    for (int i = 0; i < 20001; i++)
        ok &= values[i] >= -3.0f && values[i] < 5.0f;
    CHECK(ok, "fill_range in [min, max)");

    /* Odd count: the second value of the last pair is dropped. */
    double sum = 0, sum2 = 0;
    athena_random_fill_gaussian(&rng, values, 20001, -2.0f, 0.5f);
    for (int i = 0; i < 20001; i++) {
        sum += values[i];
        sum2 += (double)values[i] * values[i];
    }
    double mean = sum / 20001, sd = sqrt(sum2 / 20001 - mean * mean);
    CHECK(fabs(mean + 2.0) < 0.02 && fabs(sd - 0.5) < 0.02, "fill_gaussian mean %f stddev %f", mean, sd);

    /* partial_shuffle: k distinct elements, each element equally likely. */
    int chosen[10] = { 0 };
    for (int round = 0; round < 20000; round++) {
        uint8_t items[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        int seen = 0;
        athena_random_partial_shuffle(&rng, items, 10, 3, 1);
        for (int i = 0; i < 3; i++) {
            if (seen & (1 << items[i]))
                ok = 0;
            seen |= 1 << items[i];
            chosen[items[i]]++;
        }
        for (int i = 0; i < 10; i++)   /* still a permutation */
            seen |= 1 << items[i];
        ok &= seen == 0x3FF;
    }
    CHECK(ok, "partial_shuffle keeps a permutation with distinct picks");
    for (int i = 0; i < 10; i++)
        CHECK(abs(chosen[i] - 6000) < 400, "element %d sampled %d times of 6000", i, chosen[i]);
    uint8_t few[3] = { 7, 8, 9 };
    athena_random_partial_shuffle(&rng, few, 3, 10, 1);   /* k > count is clamped */
    CHECK(few[0] + few[1] + few[2] == 24, "k above count is clamped");
}

/*
 * The integer fingerprint of bin/tests/random_noise_test.js, through the C
 * API: the JavaScript runner (i386), this test (the host's arch) and the PS2
 * must all give the same value.
 */
static void test_fingerprint(void) {
    AthenaRandom rng;
    uint32_t hash = 2166136261u;

    athena_random_seed(&rng, athena_random_hash("fingerprint", 11));
    for (int i = 0; i < 256; i++) {
        int32_t value = i < 128 ? athena_random_int(&rng, -1000000, 1000000) :
            (int32_t)athena_random_u32(&rng);   /* fill() over the whole int32 range */
        hash = (hash ^ (uint32_t)value) * 16777619u;
    }
    CHECK((int32_t)hash == -1316571021, "integer fingerprint %d", (int32_t)hash);
}

typedef float (*Noise2)(const AthenaNoise *, float, float);
typedef float (*Noise3)(const AthenaNoise *, float, float, float);

static void test_noise_ranges(void) {
    static const struct { const char *name; Noise2 f2; Noise3 f3; float lo, hi; } kinds[] = {
        { "perlin2", athena_noise_perlin2, NULL, -1.05f, 1.05f },
        { "perlin3", NULL, athena_noise_perlin3, -1.05f, 1.05f },
        { "simplex2", athena_noise_simplex2, NULL, -1.0f, 1.0f },
        { "simplex3", NULL, athena_noise_simplex3, -1.0f, 1.0f },
        { "worley2", athena_noise_worley2, NULL, 0.0f, 1.5f },
        { "worley3", NULL, athena_noise_worley3, 0.0f, 1.8f },
    };
    AthenaNoise noise;

    athena_noise_seed(&noise, 3);
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        float lo = 1e9f, hi = -1e9f, jump = 0.0f;
        double sum = 0;
        int n = 0;
        for (int i = -150; i < 150; i++) {
            for (int j = -150; j < 150; j++) {
                float x = i * 0.0731f, y = j * 0.0673f, z = (i - j) * 0.0217f;
                float v = kinds[k].f2 ? kinds[k].f2(&noise, x, y) : kinds[k].f3(&noise, x, y, z);
                /* Continuity: a step of 1e-3 changes the value a little. */
                float w = kinds[k].f2 ? kinds[k].f2(&noise, x + 1e-3f, y) :
                    kinds[k].f3(&noise, x + 1e-3f, y, z);
                if (fabsf(v - w) > jump)
                    jump = fabsf(v - w);
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
                sum += v;
                n++;
            }
        }
        CHECK(lo >= kinds[k].lo && hi <= kinds[k].hi, "%s range [%f, %f]", kinds[k].name, lo, hi);
        CHECK(hi - lo > 0.6f * (kinds[k].hi - kinds[k].lo) || !strncmp(kinds[k].name, "worley", 6),
            "%s uses its range: [%f, %f]", kinds[k].name, lo, hi);
        CHECK(jump < 0.02f, "%s continuity: jump %f", kinds[k].name, jump);
        if (kinds[k].lo < 0)
            CHECK(fabs(sum / n) < 0.1, "%s mean %f", kinds[k].name, sum / n);
    }

    /* Perlin is zero on the lattice. */
    CHECK(athena_noise_perlin2(&noise, 3, -7) == 0.0f, "perlin2 lattice");
    CHECK(athena_noise_perlin3(&noise, 3, -7, 12) == 0.0f, "perlin3 lattice");
}

static void test_noise_seeds(void) {
    AthenaNoise a, b, c;
    int same = 1, differ = 0;

    athena_noise_seed(&a, 1);
    athena_noise_seed(&b, 1);
    athena_noise_seed(&c, 2);
    for (int i = 0; i < 200; i++) {
        float x = i * 0.37f, y = i * 0.23f;
        same &= athena_noise_simplex2(&a, x, y) == athena_noise_simplex2(&b, x, y) &&
            athena_noise_worley2(&a, x, y) == athena_noise_worley2(&b, x, y);
        differ += athena_noise_simplex2(&a, x, y) != athena_noise_simplex2(&c, x, y);
        differ += athena_noise_worley2(&a, x, y) != athena_noise_worley2(&c, x, y);
    }
    CHECK(same, "same seed, same noise");
    CHECK(differ > 380, "different seeds differ (%d of 400)", differ);

    /* The permutation is one: every byte appears once. */
    int count[256] = { 0 }, ok = 1;
    for (int i = 0; i < 256; i++)
        count[a.perm[i]]++;
    for (int i = 0; i < 256; i++)
        ok &= count[i] == 1 && a.perm[i] == a.perm[i + 256] && a.perm12[i] == a.perm[i] % 12;
    CHECK(ok, "perm is a permutation");
}

static void test_fbm_fill(void) {
    AthenaNoise noise;
    AthenaNoiseFractal one = { .type = ATHENA_NOISE_PERLIN, .octaves = 1, .lacunarity = 2.0f, .gain = 0.5f };
    AthenaNoiseFractal four = { .type = ATHENA_NOISE_SIMPLEX, .octaves = 4, .lacunarity = 2.0f, .gain = 0.5f };
    AthenaNoiseFill fill;
    float grid[32 * 16], left[16 * 16], right[16 * 16];
    int match = 1, in_range = 1;

    athena_noise_seed(&noise, 11);
    CHECK(athena_noise_fbm2(&noise, &one, 1.3f, 2.7f) == athena_noise_perlin2(&noise, 1.3f, 2.7f),
        "one octave is the base noise");
    float expected = (athena_noise_simplex2(&noise, 0.3f, 0.4f) +
        0.5f * athena_noise_simplex2(&noise, 0.6f, 0.8f) +
        0.25f * athena_noise_simplex2(&noise, 1.2f, 1.6f) +
        0.125f * athena_noise_simplex2(&noise, 2.4f, 3.2f)) / 1.875f;
    CHECK(fabsf(athena_noise_fbm2(&noise, &four, 0.3f, 0.4f) - expected) < 1e-5f, "fbm sums octaves");

    memset(&fill, 0, sizeof(fill));
    fill.fractal = four;
    fill.scale = 0.1f;
    fill.min = -5.0f;
    fill.max = 5.0f;
    athena_noise_fill(&noise, &fill, grid, 32, 16);
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 32; col++) {
            float v = grid[row * 32 + col];
            float n = athena_noise_fbm2(&noise, &four, col * 0.1f, row * 0.1f);
            float t = n * 0.5f + 0.5f;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            match &= fabsf(v - (-5.0f + 10.0f * t)) < 1e-4f;
            in_range &= v >= -5.0f && v <= 5.0f;
        }
    }
    CHECK(match, "fill samples (col * scale, row * scale)");
    CHECK(in_range, "fill maps onto [min, max]");

    /* Two chunks with x offsets continue the full grid. */
    athena_noise_fill(&noise, &fill, left, 16, 16);
    fill.x = 16.0f;
    athena_noise_fill(&noise, &fill, right, 16, 16);
    match = 1;
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            match &= fabsf(left[row * 16 + col] - grid[row * 32 + col]) < 1e-4f;
            match &= fabsf(right[row * 16 + col] - grid[row * 32 + 16 + col]) < 1e-4f;
        }
    }
    CHECK(match, "chunks at x = n * width tile");

    /* Worley and a 3D slice. */
    fill.x = 0;
    fill.fractal.type = ATHENA_NOISE_WORLEY;
    fill.fractal.octaves = 1;
    fill.use_z = 1;
    fill.z = 5.0f;   /* in cells: 5 * scale 0.1 = 0.5 */
    fill.min = 0;
    fill.max = 1;
    athena_noise_fill(&noise, &fill, left, 16, 16);
    match = 1;
    for (int i = 0; i < 16 * 16; i++) {
        float w = athena_noise_worley3(&noise, (i % 16) * 0.1f, (i / 16) * 0.1f, 0.5f);
        match &= fabsf(left[i] - (w > 1 ? 1 : w)) < 1e-5f;
    }
    CHECK(match, "worley 3D fill");
}

static void test_modes_warp_normalize(void) {
    static const AthenaNoiseMode modes[3] = { ATHENA_NOISE_FBM, ATHENA_NOISE_RIDGED, ATHENA_NOISE_BILLOW };
    AthenaNoise noise;
    AthenaNoiseFractal fractal;
    AthenaNoiseFill fill;
    float grid[24 * 24];

    athena_noise_fractal_default(&fractal);
    CHECK(fractal.type == ATHENA_NOISE_SIMPLEX && fractal.mode == ATHENA_NOISE_FBM &&
        fractal.octaves == 4 && fractal.lacunarity == 2.0f && fractal.gain == 0.5f &&
        fractal.warp == 0.0f, "fractal defaults");
    athena_noise_seed(&noise, 5);

    for (int m = 0; m < 3; m++) {
        for (int warped = 0; warped < 2; warped++) {
            float lo = 1e9f, hi = -1e9f, jump = 0.0f, fine = 0.0f;
            int match = 1;
            athena_noise_fractal_default(&fractal);
            fractal.mode = modes[m];
            fractal.warp = warped ? 0.8f : 0.0f;
            for (int i = 0; i < 200; i++) {
                for (int j = 0; j < 40; j++) {
                    float x = i * 0.071f, y = j * 0.093f;
                    float v = athena_noise_fbm2(&noise, &fractal, x, y);
                    float w = athena_noise_fbm2(&noise, &fractal, x + 1e-3f, y);
                    float u = athena_noise_fbm2(&noise, &fractal, x + 1e-4f, y);
                    lo = v < lo ? v : lo;
                    hi = v > hi ? v : hi;
                    jump = fabsf(v - w) > jump ? fabsf(v - w) : jump;
                    fine = fabsf(v - u) > fine ? fabsf(v - u) : fine;
                }
            }
            /* The amplitudes are divided by their sum: allow its rounding. */
            CHECK(lo >= -1.0f - 1e-5f && hi <= 1.0f + 1e-5f && hi - lo > 0.5f,
                "mode %d warp %d range [%f, %f]", m, warped, lo, hi);
            /*
             * Continuity: ridged and billow are steeper than fBm, so no fixed
             * bound; a 10x smaller step must give a much smaller change,
             * which a jump in the function would not.
             */
            CHECK(jump < 0.2f && fine < jump * 0.3f,
                "mode %d warp %d continuity: %f at 1e-3, %f at 1e-4", m, warped, jump, fine);

            /* The grid loop computes exactly what fbm2 computes. */
            memset(&fill, 0, sizeof(fill));
            fill.fractal = fractal;
            fill.scale = 0.1f;
            fill.max = 1.0f;
            athena_noise_fill(&noise, &fill, grid, 24, 24);
            for (int i = 0; i < 24 * 24; i++) {
                float n = athena_noise_fbm2(&noise, &fractal, (i % 24) * 0.1f, (i / 24) * 0.1f);
                float t = n * 0.5f + 0.5f;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                match &= fabsf(grid[i] - t) < 1e-5f;
            }
            CHECK(match, "fill matches fbm2, mode %d warp %d", m, warped);
        }
    }

    /* Warp moves the samples, and 3D fill with warp matches fbm3. */
    athena_noise_fractal_default(&fractal);
    float plain = athena_noise_fbm2(&noise, &fractal, 1.3f, 2.1f);
    fractal.warp = 0.8f;
    CHECK(athena_noise_fbm2(&noise, &fractal, 1.3f, 2.1f) != plain, "warp changes the value");
    memset(&fill, 0, sizeof(fill));
    fill.fractal = fractal;
    fill.scale = 0.1f;
    fill.use_z = 1;
    fill.z = 3.0f;
    fill.max = 1.0f;
    athena_noise_fill(&noise, &fill, grid, 24, 24);
    int match = 1;
    for (int i = 0; i < 24 * 24; i++) {
        float n = athena_noise_fbm3(&noise, &fractal, (i % 24) * 0.1f, (i / 24) * 0.1f, 3.0f * 0.1f);
        float t = n * 0.5f + 0.5f;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        match &= fabsf(grid[i] - t) < 1e-5f;
    }
    CHECK(match, "3D warped fill matches fbm3");

    /* normalize: the values produced span [min, max] exactly. */
    athena_noise_fractal_default(&fractal);
    fractal.type = ATHENA_NOISE_PERLIN;
    memset(&fill, 0, sizeof(fill));
    fill.fractal = fractal;
    fill.scale = 0.07f;
    fill.min = 10.0f;
    fill.max = 20.0f;
    fill.normalize = 1;
    athena_noise_fill(&noise, &fill, grid, 24, 24);
    float lo = grid[0], hi = grid[0];
    for (int i = 1; i < 24 * 24; i++) {
        lo = grid[i] < lo ? grid[i] : lo;
        hi = grid[i] > hi ? grid[i] : hi;
    }
    CHECK(lo == 10.0f && fabsf(hi - 20.0f) < 1e-4f, "normalize spans [%f, %f]", lo, hi);
    fill.normalize = 0;
    athena_noise_fill(&noise, &fill, grid, 24, 24);
    lo = grid[0];
    hi = grid[0];
    for (int i = 1; i < 24 * 24; i++) {
        lo = grid[i] < lo ? grid[i] : lo;
        hi = grid[i] > hi ? grid[i] : hi;
    }
    CHECK(lo > 10.0f && hi < 20.0f, "without normalize perlin stays inside: [%f, %f]", lo, hi);

    /* A flat input (one cell) maps to min, not NaN. */
    fill.normalize = 1;
    athena_noise_fill(&noise, &fill, grid, 1, 1);
    CHECK(grid[0] == 10.0f, "normalize of one value gives min: %f", grid[0]);
}

static void test_to_tiles(void) {
    const float values[7] = { -1.0f, 0.0f, 0.29f, 0.3f, 0.5f, 0.79f, 2.0f };
    const float thresholds[3] = { 0.3f, 0.5f, 0.8f };
    const uint16_t tiles[4] = { 10, 20, 30, 0xFFFF };
    const uint16_t expected[7] = { 10, 10, 10, 20, 30, 30, 0xFFFF };
    uint16_t out[7];

    athena_noise_to_tiles(out, values, 7, thresholds, tiles, 3);
    CHECK(!memcmp(out, expected, sizeof(out)), "toTiles %u %u %u %u %u %u %u",
        out[0], out[1], out[2], out[3], out[4], out[5], out[6]);
    athena_noise_to_tiles(out, values, 7, NULL, tiles, 0);
    CHECK(out[0] == 10 && out[6] == 10, "no thresholds: first tile");
}

int main(void) {
    test_reference();
    test_determinism();
    test_int();
    test_floats();
    test_weighted_shuffle();
    test_batches_and_samples();
    test_fingerprint();
    test_noise_ranges();
    test_noise_seeds();
    test_fbm_fill();
    test_modes_warp_normalize();
    test_to_tiles();

    if (failures) {
        printf("random_noise_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("random_noise_test: all checks passed\n");
    return 0;
}
