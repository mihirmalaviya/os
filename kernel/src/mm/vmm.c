#include "mm/vmm.h"
#include "mm/pmm.h"
#include "string.h"
#include "terminal/terminal.h"
#include "kernel.h"
#include <stdint.h>
#include <limine.h>

#define PAGE_SIZE 4096ULL

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kaddr_req = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

extern char __kernel_start[], __kernel_end[];
extern char *fb;
extern uint64_t fb_size;

uint64_t *vmm_new_pagemap(void) {
    return (uint64_t *)(pmm_alloc() + pmm_hhdm_offset);
}

static uint64_t *current_pml4;

uint64_t *vmm_get_current_pml4(void) {
    return (uint64_t *)((read_cr3() & ~0xFFFULL) + pmm_hhdm_offset);
}

static uint64_t *get_or_create(uint64_t *table, uint64_t index) {
    if (table[index] & VMM_PRESENT) {
        return (uint64_t *)((table[index] & ~0xFFF) + pmm_hhdm_offset);
    }
    uint64_t new_table = pmm_alloc();
    table[index] = new_table | VMM_PRESENT | VMM_WRITABLE;
    return (uint64_t *)(new_table + pmm_hhdm_offset);
}

// walks the table and sets the flags at the location
void vmm_map(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create(pml4, pml4_i);
    uint64_t *pd   = get_or_create(pdpt, pdpt_i);
    uint64_t *pt   = get_or_create(pd,   pd_i);

		// this should never happen if the code is working correctly
    if (pt[pt_i] & VMM_PRESENT) {
        kprintf("vmm: remapping already-present page at %llx\n", virt);
        hcf();
    }

		// kprintf("heres whats in this pt entry: %x\n", pt[pt_i]);

    pt[pt_i] = phys | flags;
}

void vmm_unmap(uint64_t *pml4, uint64_t virt) {
    uint64_t pml4_i = (virt >> 39) & 0x1FF;
    uint64_t pdpt_i = (virt >> 30) & 0x1FF;
    uint64_t pd_i   = (virt >> 21) & 0x1FF;
    uint64_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create(pml4, pml4_i);
    uint64_t *pd   = get_or_create(pdpt, pdpt_i);
    uint64_t *pt   = get_or_create(pd,   pd_i);

		uint64_t phys = pt[pt_i];
    pt[pt_i] &= ~VMM_PRESENT;
    invlpg(virt);
		pmm_free(phys);
}

void vmm_switch(uint64_t *pml4) {
    uint64_t phys = (uint64_t)pml4 - pmm_hhdm_offset;
    write_cr3(phys);
    current_pml4 = pml4;
}

void vmm_init(void) {

		if (kaddr_req.response == NULL) {
				panic("vmm: kaddr request failed");
		}

		// read the current page table
		current_pml4 = vmm_get_current_pml4();
    // current_pml4 = vmm_new_pagemap();

    // // map all physical memory at the HHDM offset
    // for (uint64_t i = 0; i < pmm_total_pages; i++) {
    //     uint64_t phys = i * PAGE_SIZE;
    //     vmm_map(current_pml4, phys + pmm_hhdm_offset, phys, VMM_PRESENT | VMM_WRITABLE);
    // }

    // // map the kernel (virtual base -> physical base, page by page)
    // uint64_t kphys = kaddr_req.response->physical_base;
    // uint64_t kvirt = kaddr_req.response->virtual_base;
    // uint64_t ksize = (uint64_t)__kernel_end - (uint64_t)__kernel_start;

    // for (uint64_t i = 0; i < ksize; i += PAGE_SIZE)
    //     vmm_map(current_pml4, kvirt + i, kphys + i, VMM_PRESENT | VMM_WRITABLE);

    vmm_switch(current_pml4);
}
