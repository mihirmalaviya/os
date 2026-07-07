#pragma once
#include <stdint.h>

extern uint64_t pmm_hhdm_offset;

void     pmm_init(void);
uint64_t pmm_alloc(void);   // returns physical address, 0 if OOM
void     pmm_free(uint64_t phys);

extern uint64_t pmm_total_pages;
