#include "mm/dma.h"
#include "mm/pmm.h"

dma_buf_t dma_alloc(size_t pages) {
    dma_buf_t buf;

    buf.phys = pmm_alloc_contig(pages);
    if (buf.phys != 0) {
        buf.virt = (void *)(buf.phys + pmm_hhdm_offset);
    }

    return buf;
}
