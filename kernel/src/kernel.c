#include "kernel.h"
#include "terminal/terminal.h"

void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

void panic(const char *msg) {
    kprintf("KERNEL PANIC: %s\n", msg);
    hcf();
}
