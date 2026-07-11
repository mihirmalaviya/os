#include "arch/isr.h"
#include "arch/pic.h"
#include "arch/pit.h"
#include "drivers/keyboard.h"
#include "terminal/terminal.h"
#include "sched/task.h"

void isr_handler(interrupt_frame_t *frame) {
    // vector is the number in the IDT table
    switch (frame->vector) {
        case 0:
            kprintf("divide by zero at RIP: %llx\n", frame->rip);
            for (;;) asm ("hlt");

        case 14:
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

        case 32:
            PIC_sendEOI(frame->vector - 32);
            PIT_IRQ_handler();
            break;
        case 33:
            keyboard_handle_irq();
            PIC_sendEOI(frame->vector - 32);
            break;
        default:
            break;
    }

    check_postponed_switch();
}
