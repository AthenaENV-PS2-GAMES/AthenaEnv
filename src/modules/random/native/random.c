#include <math.h>
#include <string.h>
#include <time.h>

#ifdef _EE
#include <timer.h>
#endif

#include <athena/random.h>

/*
 * The EE's FPU has no infinities nor NaN: it compares their bit patterns as
 * large numbers, so isinf()/isnan() and NaN-false comparisons fail there.
 */
static inline bool finite_float(float x) {
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

static inline uint32_t rotl(uint32_t x, int k) {
    return (x << k) | (x >> (32 - k));
}

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void athena_random_seed(AthenaRandom *rng, uint64_t seed) {
    uint64_t a = splitmix64(&seed);
    uint64_t b = splitmix64(&seed);

    rng->s[0] = (uint32_t)a;
    rng->s[1] = (uint32_t)(a >> 32);
    rng->s[2] = (uint32_t)b;
    rng->s[3] = (uint32_t)(b >> 32);
    /* splitmix64 is a bijection: two consecutive outputs are never both 0. */
}

bool athena_random_set_state(AthenaRandom *rng,
    const uint32_t state[ATHENA_RANDOM_STATE_WORDS]) {
    if (!(state[0] | state[1] | state[2] | state[3]))
        return false;
    memcpy(rng->s, state, sizeof(rng->s));
    return true;
}

uint64_t athena_random_entropy(void) {
    static uint64_t counter;
#ifdef _EE
    uint64_t value = GetTimerSystemTime();
#else
    uint64_t value = (uint64_t)clock() ^ ((uint64_t)time(NULL) << 32);
#endif

    /* Workers may race on the counter: it only mixes, any value will do. */
    counter += 0x9E3779B97F4A7C15ULL;
    return value ^ counter ^ (uint64_t)(uintptr_t)&value;
}

uint64_t athena_random_hash(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint64_t hash = 0xCBF29CE484222325ULL;

    for (size_t i = 0; i < length; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

uint32_t athena_random_u32(AthenaRandom *rng) {
    uint32_t *s = rng->s;
    uint32_t result = rotl(s[1] * 5, 7) * 9;
    uint32_t t = s[1] << 9;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 11);
    return result;
}

/* Lemire's nearly divisionless method: the 32x32->64 product is one multu. */
uint32_t athena_random_below(AthenaRandom *rng, uint32_t bound) {
    uint64_t m;
    uint32_t low;

    if (bound == 0)
        return 0;
    m = (uint64_t)athena_random_u32(rng) * bound;
    low = (uint32_t)m;
    if (low < bound) {
        uint32_t threshold = -bound % bound;
        while (low < threshold) {
            m = (uint64_t)athena_random_u32(rng) * bound;
            low = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32);
}

int32_t athena_random_int(AthenaRandom *rng, int32_t min, int32_t max) {
    uint32_t span;

    if (min > max) {
        int32_t t = min;
        min = max;
        max = t;
    }
    span = (uint32_t)max - (uint32_t)min + 1u;
    /* span wraps to 0 for the full int32 range: any 32 bits will do. */
    return (int32_t)((uint32_t)min +
        (span ? athena_random_below(rng, span) : athena_random_u32(rng)));
}

float athena_random_float(AthenaRandom *rng) {
    return (float)(athena_random_u32(rng) >> 8) * (1.0f / 16777216.0f);
}

float athena_random_range(AthenaRandom *rng, float min, float max) {
    float value;

    if (!finite_float(min) || !finite_float(max) || !(max > min))
        return min;
    value = min + (max - min) * athena_random_float(rng);
    /* Rounding can land on max when the range is wide next to its bounds. */
    return value < max ? value : nextafterf(max, min);
}

bool athena_random_bool(AthenaRandom *rng, float p) {
    if (!finite_float(p) || !(p > 0.0f))
        return false;
    if (p >= 1.0f)
        return true;
    return athena_random_float(rng) < p;
}

float athena_random_gaussian(AthenaRandom *rng, float mean, float stddev) {
    float u, v, s;

    do {
        u = athena_random_float(rng) * 2.0f - 1.0f;
        v = athena_random_float(rng) * 2.0f - 1.0f;
        s = u * u + v * v;
    } while (s >= 1.0f || s == 0.0f);
    return mean + stddev * u * sqrtf(-2.0f * logf(s) / s);
}

int athena_random_weighted(AthenaRandom *rng, const float *weights,
    size_t count) {
    float total = 0.0f, target, sum = 0.0f;
    int last = -1;

    for (size_t i = 0; i < count; i++) {
        if (!finite_float(weights[i]) || weights[i] < 0.0f)
            return -1;
        total += weights[i];
    }
    if (!finite_float(total) || !(total > 0.0f))
        return -1;
    target = athena_random_float(rng) * total;
    for (size_t i = 0; i < count; i++) {
        if (weights[i] <= 0.0f)
            continue;
        sum += weights[i];
        last = (int)i;
        if (target < sum)
            return last;
    }
    /* Float rounding left target at the very top: the last positive weight. */
    return last;
}

static inline void swap_bytes(uint8_t *a, uint8_t *b, size_t size) {
    for (size_t k = 0; k < size; k++) {
        uint8_t t = a[k];
        a[k] = b[k];
        b[k] = t;
    }
}

float athena_random_angle(AthenaRandom *rng) {
    /* float(2 pi) is above 2 pi: keep the result below the true bound. */
    float angle = athena_random_float(rng) * 6.2831853f;
    return angle < 6.2831850f ? angle : 0.0f;
}

void athena_random_shuffle(AthenaRandom *rng, void *base, size_t count,
    size_t size) {
    uint8_t *bytes = base;

    for (size_t i = count; i > 1; i--) {
        size_t j = athena_random_below(rng, (uint32_t)i);

        if (j != i - 1)
            swap_bytes(bytes + (i - 1) * size, bytes + j * size, size);
    }
}

void athena_random_partial_shuffle(AthenaRandom *rng, void *base,
    size_t count, size_t k, size_t size) {
    uint8_t *bytes = base;

    if (k > count)
        k = count;
    for (size_t i = 0; i < k; i++) {
        size_t j = i + athena_random_below(rng, (uint32_t)(count - i));

        if (j != i)
            swap_bytes(bytes + i * size, bytes + j * size, size);
    }
}

void athena_random_fill_range(AthenaRandom *rng, float *out, size_t count,
    float min, float max) {
    for (size_t i = 0; i < count; i++)
        out[i] = athena_random_range(rng, min, max);
}

/* The polar method makes two normal values per accepted pair: use both. */
void athena_random_fill_gaussian(AthenaRandom *rng, float *out, size_t count,
    float mean, float stddev) {
    size_t i = 0;

    while (i < count) {
        float u, v, s, factor;

        do {
            u = athena_random_float(rng) * 2.0f - 1.0f;
            v = athena_random_float(rng) * 2.0f - 1.0f;
            s = u * u + v * v;
        } while (s >= 1.0f || s == 0.0f);
        factor = stddev * sqrtf(-2.0f * logf(s) / s);
        out[i++] = mean + u * factor;
        if (i < count)
            out[i++] = mean + v * factor;
    }
}
