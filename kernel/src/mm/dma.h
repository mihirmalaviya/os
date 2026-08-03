#pragma once
#include <stdint.h>
#include <stddef.h>
#include "mm/pmm.h"

// any hhdm-mapped virtual address back to its physical address. a property of
// the mapping, not of any one buffer, so it works on a pointer into the middle
// of one too.
#define KPHYS(v) ((uint64_t)(v) - pmm_hhdm_offset)

typedef struct {
    uint64_t phys;
    void *virt;
} dma_buf_t;

// allocates n contiguous pages usable for dma; virt is NULL on failure
dma_buf_t dma_alloc(size_t pages);
