#ifndef ATHENA_RANDOM_H
#define ATHENA_RANDOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Seedable pseudo-random generator: xoshiro128** (Blackman and Vigna), 128
 * bits of state and period 2^128 - 1. It only uses 32-bit operations, which
 * the EE runs natively; generators built on 64-bit products (PCG, splitmix64)
 * call a software multiply on every number.
 *
 *     AthenaRandom rng;
 *     athena_random_seed(&rng, 1234);
 *     int die = athena_random_int(&rng, 1, 6);
 *
 * The same seed always gives the same sequence on every platform. The state
 * is four plain words: copy the struct to save it and to fork a sequence.
 */

#define ATHENA_RANDOM_STATE_WORDS 4

typedef struct {
    uint32_t s[ATHENA_RANDOM_STATE_WORDS];
} AthenaRandom;

/* Expands a 64-bit seed into the state (splitmix64). Any seed is valid. */
void athena_random_seed(AthenaRandom *rng, uint64_t seed);

/*
 * Sets the state directly. Returns false, leaving the generator unchanged,
 * for the all-zero state, the one state xoshiro cannot leave.
 */
bool athena_random_set_state(AthenaRandom *rng,
    const uint32_t state[ATHENA_RANDOM_STATE_WORDS]);

/*
 * A seed that differs on every call, from the EE bus clock (the C clock on
 * other platforms) and a counter: for generators that need not reproduce.
 */
uint64_t athena_random_entropy(void);

/* 64-bit FNV-1a hash, to seed from text ("seed words"). */
uint64_t athena_random_hash(const void *data, size_t length);

/* Next 32 random bits. */
uint32_t athena_random_u32(AthenaRandom *rng);

/* Unbiased integer in [0, bound); 0 when bound is 0. */
uint32_t athena_random_below(AthenaRandom *rng, uint32_t bound);

/* Unbiased integer in [min, max], both inclusive. Swaps reversed bounds. */
int32_t athena_random_int(AthenaRandom *rng, int32_t min, int32_t max);

/* Float in [0, 1), with 24 random bits: every value is exact in a float. */
float athena_random_float(AthenaRandom *rng);

/* Float in [min, max); min when the range is empty or a bound is not finite. */
float athena_random_range(AthenaRandom *rng, float min, float max);

/* True with probability `p` (clamped to [0, 1]); false for NaN. */
bool athena_random_bool(AthenaRandom *rng, float p);

/* Normally distributed float (Marsaglia polar method). */
float athena_random_gaussian(AthenaRandom *rng, float mean, float stddev);

/*
 * Index in [0, count) drawn with probability proportional to its weight.
 * Weights must be finite and non-negative; returns -1 when they are not, or
 * when none is positive.
 */
int athena_random_weighted(AthenaRandom *rng, const float *weights,
    size_t count);

/* Angle in radians, in [0, 2 pi). */
float athena_random_angle(AthenaRandom *rng);

/* Fisher-Yates shuffle of `count` elements of `size` bytes, in place. */
void athena_random_shuffle(AthenaRandom *rng, void *base, size_t count,
    size_t size);

/*
 * Shuffles only the first `k` positions (k <= count): they end up a uniform
 * random sample of the elements, in random order. Costs k draws, not count.
 */
void athena_random_partial_shuffle(AthenaRandom *rng, void *base,
    size_t count, size_t k, size_t size);

/* Batches: `count` floats in [min, max), or normally distributed. */
void athena_random_fill_range(AthenaRandom *rng, float *out, size_t count,
    float min, float max);
void athena_random_fill_gaussian(AthenaRandom *rng, float *out, size_t count,
    float mean, float stddev);

#endif /* ATHENA_RANDOM_H */
