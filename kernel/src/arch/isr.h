#pragma once
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

void isr_handler(interrupt_frame_t *frame);

typedef void (*irq_handler_t)(void *ctx);
void irq_register(uint8_t vector, irq_handler_t handler);
