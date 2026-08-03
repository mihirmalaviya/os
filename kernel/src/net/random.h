#pragma once
#include <stdint.h>

uint32_t rand32(void); // full 32-bit range, not RAND_MAX-limited
void srand(uint32_t seed);
