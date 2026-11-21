#include <stddef.h>
#include "prng.h"

/**
 * Modified version of Sebastiano Vigna's SplitMix64 generator
 * Adapted from https://xoshiro.di.unimi.it/splitmix64.c
 *
 * Warning: modifies `seed`!
 */
uint64_t splitmix64(uint64_t* seed) {
	uint64_t z = (*seed += 0x9e3779b97f4a7c15ULL);
	z          = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z          = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

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
uint64_t xoshiro256ss(uint64_t s[static 4]) {
	const uint64_t result = rotl(s[1] * 5, 7) * 9;

	const uint64_t t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 45);

	return result;
}

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
void jump(uint64_t s[static 4]) {
	static const uint64_t JUMP[] = { 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c,
		                         0xa9582618e03fc9aa,
		                         0x39abdc4529b1661c };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	uint64_t s2 = 0;
	uint64_t s3 = 0;
	for (size_t i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
		for (size_t b = 0; b < 64; b++) {
			if (JUMP[i] & UINT64_C(1) << b) {
				s0 ^= s[0];
				s1 ^= s[1];
				s2 ^= s[2];
				s3 ^= s[3];
			}
			xoshiro256ss(s);
		}

	s[0] = s0;
	s[1] = s1;
	s[2] = s2;
	s[3] = s3;
}

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
void long_jump(uint64_t s[static 4]) {
	static const uint64_t LONG_JUMP[] = { 0x76e15d3efefdcbbf,
		                              0xc5004e441c522fb3,
		                              0x77710069854ee241,
		                              0x39109bb02acbe635 };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	uint64_t s2 = 0;
	uint64_t s3 = 0;
	for (size_t i = 0; i < sizeof LONG_JUMP / sizeof *LONG_JUMP; i++)
		for (size_t b = 0; b < 64; b++) {
			if (LONG_JUMP[i] & UINT64_C(1) << b) {
				s0 ^= s[0];
				s1 ^= s[1];
				s2 ^= s[2];
				s3 ^= s[3];
			}
			xoshiro256ss(s);
		}

	s[0] = s0;
	s[1] = s1;
	s[2] = s2;
	s[3] = s3;
}
