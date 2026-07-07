#include <stdint.h>
#include "arch/idt.h"
#include "arch/isr.h"

typedef struct {
    uint16_t base_low;  // The lower 16 bits of the ISR's address
    uint16_t kernel_cs; // The GDT segment selector that the CPU will load into
                        // CS before calling the ISR
    uint8_t ist; // The IST in the TSS that the CPU will load into RSP; set to
                 // zero for now
    uint8_t attributes; // Type and attributes; see the IDT page
    uint16_t base_mid;  // The higher 16 bits of the lower 32 bits of the ISR's
                        // address
    uint32_t base_high; // The higher 32 bits of the ISR's address
    uint32_t reserved;  // Set to zero
} __attribute__((packed)) idt_entry_t;

static idt_entry_t idt[256];

extern void *isr_stub_table[];

void idt_set(uint8_t vector, void *handler, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vector] = (idt_entry_t){
        .base_low   = addr & 0xffff,
        .kernel_cs  = 0x08,
        .ist        = 0,
        .attributes = type_attr,
        .base_mid   = (addr >> 16) & 0xffff,
        .base_high  = (addr >> 32) & 0xffffffff,
        .reserved   = 0,
    };
}

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        idt_set(i, isr_stub_table[i], 0x8E);

    idtr_t idtr = {
        .limit = sizeof(idt) - 1,
        .base  = (uint64_t)idt,
    };
    idt_load(&idtr);
}
