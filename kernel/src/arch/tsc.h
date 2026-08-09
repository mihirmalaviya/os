#pragma once
#include <stdint.h>

uint64_t rdtsc(void);

void tsc_calibrate(void); // must run before sti()
uint64_t tsc_ms(void);
