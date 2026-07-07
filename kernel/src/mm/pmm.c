#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "mm/pmm.h"
#include "memory.h"
#include "terminal/terminal.h"
#include "kernel.h"

#define PAGE_SIZE 4096ULL

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

uint64_t pmm_hhdm_offset;
uint64_t pmm_total_pages;

static uint8_t *bitmap;

static void bitmap_set(uint64_t page)   { bitmap[page / 8] |=  (1 << (page % 8)); }
static void bitmap_clear(uint64_t page) { bitmap[page / 8] &= ~(1 << (page % 8)); }
static int  bitmap_get(uint64_t page)   { return bitmap[page / 8] & (1 << (page % 8)); }

void pmm_init(void) {
	if (hhdm_req.response == NULL) {
		panic("pmm: hhdm request failed");
	}
	if (memmap_req.response == NULL) {
		panic("pmm: memmap request failed");
	}

	pmm_hhdm_offset = hhdm_req.response->offset;
	struct limine_memmap_response *mm = memmap_req.response;

	// see how many pages we need to map out
	uint64_t highest = 0;
	for (size_t i = 0; i < mm->entry_count; i++) {
		struct limine_memmap_entry *e = mm->entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE) continue;

		uint64_t top = e->base + e->length;
		if (top > highest) highest = top;
	}

	// see where we can store our bitmap
	pmm_total_pages = highest / PAGE_SIZE;
	size_t bitmap_bytes = (pmm_total_pages + 7) / 8;

	for (size_t i = 0; i < mm->entry_count; i++) {
		struct limine_memmap_entry *e = mm->entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE) continue;

		if (e->length >= bitmap_bytes) {
			bitmap = (uint8_t *)(e->base + pmm_hhdm_offset);
			break;
		}
	}

	memset(bitmap, 0xff, bitmap_bytes); // set everything to used

	// mark the stuff that is free as 0
	for (size_t i = 0; i < mm->entry_count; i++) {
		struct limine_memmap_entry *e = mm->entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE) continue;

		for (uint64_t j = 0; j < e->length / PAGE_SIZE; j++)
			bitmap_clear(e->base / PAGE_SIZE + j);
	}

	uint64_t bitmap_phys = (uint64_t)bitmap - pmm_hhdm_offset;
	for (size_t i = 0; i < (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE; i++)
		bitmap_set(bitmap_phys / PAGE_SIZE + i);

	// for (uint64_t i = 0; i < pmm_total_pages; i++) {
	// 	kprintf(bitmap_get(i) ? "1" : "0");
	// }

	// kprintf("%x\n",pmm_hhdm_offset);
	// kprintf("%x\n",bitmap);
}

// need to optimize this; or change to using a buddy allocator or sm
uint64_t pmm_alloc(void) {
	for (size_t i=0; i<pmm_total_pages; i++){
		if (bitmap_get(i)==0){
			bitmap_set(i);
			uint64_t phys = i * PAGE_SIZE;
			memset((uint8_t *)(phys + pmm_hhdm_offset), 0, PAGE_SIZE);
			return phys;
		}
	}
	return 0;
}

void pmm_free(uint64_t phys) {
	bitmap_clear(phys / PAGE_SIZE);
}
