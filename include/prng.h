#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>

/**
 * Modified version of Sebastiano Vigna's SplitMix64 generator
 * Adapted from https://xoshiro.di.unimi.it/splitmix64.c
 *
 * Warning: modifies `seed`!
 */
uint64_t splitmix64(uint64_t* seed);

/**
 * David Blackman and Sebastiano Vigna's xoshiro256** generator
 * See https://prng.di.unimi.it/xoshiro256starstar.c
 *
 * Warning: modifies `s`!
 *
 * From https://prng.di.unimi.it/
 * 	>We suggest to use SplitMix64 to initialize the state of our generators
 * 	>starting from a 64-bit seed
 */
uint64_t xoshiro256ss(uint64_t s[static 4]);
/**
 * From David Blackman and Sebastiano Vigna's xoshiro256** generator
 * 	https://prng.di.unimi.it/xoshiro256starstar.c
 *
 * Warning: modifies `s`!
 *
 * 	>This is the jump function for the generator. It is equivalent
 * 	>to 2^128 calls to next(); it can be used to generate 2^128
 * 	>non-overlapping subsequences for parallel computations.
 */
void jump(uint64_t s[static 4]);
/**
 * From David Blackman and Sebastiano Vigna's xoshiro256** generator
 * 	https://prng.di.unimi.it/xoshiro256starstar.c
 *
 * Warning: modifies `s`!
 *
 * 	>This is the long-jump function for the generator. It is equivalent to
 * 	>2^192 calls to next(); it can be used to generate 2^64 starting points,
 * 	>from each of which jump() will generate 2^64 non-overlapping
 * 	>subsequences for parallel distributed computations.
 */
void long_jump(uint64_t s[static 4]);

#endif
