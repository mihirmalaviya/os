#include "arch/isr.h"
#include "terminal/terminal.h"
#include "sched/task.h"

static void divide_by_zero_handler(void *ctx) {
    interrupt_frame_t *frame = (interrupt_frame_t *)ctx;

    kprintf("divide by zero at RIP: %llx\n", frame->rip);
    for (;;) asm ("hlt");
}

static void page_fault_handler(void *ctx) {
    interrupt_frame_t *frame = (interrupt_frame_t *)ctx;
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    kprintf("PAGE FAULT\n");
    kprintf("  addr (cr2) = %llx\n", cr2);
    kprintf("  rip        = %llx\n", frame->rip);
    kprintf("  error_code = %llx\n", frame->error_code);
    kprintf("  %s, %s, %s\n",
            (frame->error_code & 1) ? "protection" : "not-present",
            (frame->error_code & 2) ? "write" : "read",
            (frame->error_code & 4) ? "user" : "kernel");

    for (;;) asm ("hlt");
}

static irq_handler_t irq_handlers[256] = {
    [0]  = divide_by_zero_handler,
    [14] = page_fault_handler,
};

void irq_register(uint8_t vector, irq_handler_t handler) {
    irq_handlers[vector] = handler;
}

void isr_handler(interrupt_frame_t *frame) {
    if (irq_handlers[frame->vector]) {
        irq_handlers[frame->vector](frame);
    }

    check_postponed_switch();
}
