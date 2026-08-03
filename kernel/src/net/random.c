#include "net/random.h"

// xoroshiro64**, written in 2018 by David Blackman and Sebastiano Vigna
// (vigna@acm.org), released into the public domain. from prng.di.unimi.it

static uint32_t s[2] = {1, 0};

static inline uint32_t rotl(const uint32_t x, int k) {
    return (x << k) | (x >> (32-k));
}

uint32_t rand32(void) {
    const uint32_t s0 = s[0];
    uint32_t s1 = s[1];
    const uint32_t result = rotl(s0 * 0x9E3779BB, 5) * 5;

    s1 ^= s0;
    s[0] = rotl(s0, 26) ^ s1 ^ (s1<<9);
    s[1] = rotl(s1, 13);

    return result;
}

void srand(uint32_t seed) {
    s[0] = seed!=0 ? seed : 1; // state cant be all-zero
    s[1] = seed ^ 0x9E3779BB;
}
