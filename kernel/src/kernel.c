#include "kernel.h"
#include "terminal/terminal.h"

void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

void panic(const char *msg) {
    mprintf("KERNEL PANIC: %s\n", msg);
    hcf();
}

void assert_impl(const char *function, int line, bool condition, const char *condition_str, const char *msg) {
    if (!condition) {
        if (msg[0])
            mprintf("--- [ PANIC @ %s():%d ] --- Condition <%s> failed: %s\n",
                    function, line, condition_str, msg);
        else
            mprintf("--- [ PANIC @ %s():%d ] --- Condition <%s> failed!\n",
                    function, line, condition_str);
        hcf();
    }
}

