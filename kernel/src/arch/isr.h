#pragma once
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

void isr_handler(interrupt_frame_t *frame);

extern volatile uint64_t timer_ticks;
void sleep_ticks(uint64_t ticks);
