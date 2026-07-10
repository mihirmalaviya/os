#include <stdint.h>
#include "arch/gdt.h"
#include <string.h>

// {base, limit, access, flags}
// {0x00000000, 0x00000, 0x00, 0x0}  // null
// {0x00000000, 0xfffff, 0x9b, 0xa}  // kernel code
// {0x00000000, 0xfffff, 0x93, 0xa}  // kernel data
// {0x00000000, 0xfffff, 0xfb, 0xa}  // user code
// {0x00000000, 0xfffff, 0xf3, 0xa}  // user data

#define KERNEL_STACK_SIZE (16 * 1024)

static uint64_t gdt[] = {
    0x0000000000000000,  // 0x00 null
    0x00af9b000000ffff,  // 0x08 kernel code
    0x00af93000000ffff,  // 0x10 kernel data
    0x00affb000000ffff,  // 0x18 user code
    0x00aff3000000ffff,  // 0x20 user data
    0x0000000000000000,  // 0x28 tss low  (filled in at runtime, base isn't known at compile time)
    0x0000000000000000,  //      tss high
};

static gdtr_t gdtr = {
    .limit = sizeof(gdt) - 1,
    .base  = (uint64_t)gdt,
};

static tss_t tss;
static uint8_t kernel_stack[KERNEL_STACK_SIZE];

uint64_t *tss_rsp0_ptr;

static void tss_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.rsp0       = (uint64_t)(kernel_stack + KERNEL_STACK_SIZE);
    tss.iomap_base = sizeof(tss_t);
    tss_rsp0_ptr   = &tss.rsp0;   // <-- new line

    uint64_t base  = (uint64_t)&tss;
    uint64_t limit = sizeof(tss_t) - 1;

    gdt[5] = (limit & 0xffff)
           | ((base & 0xffffff) << 16)
           | (0x89ULL << 40)
           | (((limit >> 16) & 0xf) << 48)
           | (((base >> 24) & 0xff) << 56);
    gdt[6] = (base >> 32) & 0xffffffff;
}

void gdt_init(void) {
    tss_init();
    gdt_load(&gdtr);
    tss_load();
}
