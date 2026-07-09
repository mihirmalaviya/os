#pragma once
#include <stdint.h>

void pit_init();
void pit_tick(void);
uint64_t get_time_since_boot(void);

void sleep_ticks(uint64_t ticks);
