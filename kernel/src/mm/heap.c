#include "mm/heap.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "kernel.h"

#define PAGE_SIZE  4096ULL
#define HEAP_START 0x444444440000ULL
#define HEAP_SIZE  (4ULL * 1024 * 1024)

static uint64_t heap_cursor = HEAP_START;
static uint64_t heap_mapped_until = HEAP_START;

void heap_init(void) {
}

// BUMP ALLOCATOR
void *kmalloc(size_t size) {
    size = (size + 15) & ~15ULL; // round up to 16 bytes

		if (heap_cursor+size > HEAP_START+HEAP_SIZE) {
				panic("kmalloc: heap exhausted");
				return NULL;
		}

		uint64_t end = heap_cursor + size;
		while (heap_mapped_until < end) {
				uint64_t phys = pmm_alloc();
				vmm_map(vmm_get_current_pml4(), heap_mapped_until, phys, VMM_PRESENT | VMM_WRITABLE);
				heap_mapped_until += PAGE_SIZE;
		}

		void *ptr = (void *)heap_cursor;
		heap_cursor += size;
		return ptr;
}

void kfree(void *ptr){
	// pass
}
