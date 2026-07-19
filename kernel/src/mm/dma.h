#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t phys;
    void *virt;
} dma_buf_t;

// allocates n contiguous pages usable for dma; virt is NULL on failure
dma_buf_t dma_alloc(size_t pages);
