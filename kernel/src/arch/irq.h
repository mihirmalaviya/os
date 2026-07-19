#pragma once
#include <stdint.h>

uint64_t irq_save(void); // disable interrupts, returns flags
void     irq_restore(uint64_t flags); // restore flags saved by irq_save

void cli(void); // disable interrupts
void sti(void); // enable interrupts
