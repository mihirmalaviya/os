#pragma once
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

void idt_set(uint8_t vector, void *handler, uint8_t type_attr);
void idt_init(void);
void idt_load(idtr_t *idtr);
