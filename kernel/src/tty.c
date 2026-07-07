#include "tty.h"
#include "terminal/terminal.h"
#include <stddef.h>

#define TTY_BUF_SIZE 256

static char buf[TTY_BUF_SIZE];
static volatile size_t head = 0; // next write slot (producer: keyboard IRQ)
static volatile size_t tail = 0; // next read slot  (consumer: tty_poll)

void tty_enqueue(char c) {
    size_t next = (head + 1) % TTY_BUF_SIZE;
    if (next == tail) return; // buffer full, drop the character
    buf[head] = c;
    head = next;
}

static void print_prompt(void) {
    kputchar('$');
    kputchar(' ');
}

void tty_init(void) {
    print_prompt();
}

void tty_clear(void) {
    terminal_clear();
    head = 0;
    tail = 0;
    print_prompt();
}

void tty_poll(void) {
    while (tail != head) {
        char c = buf[tail];
        kputchar(c);
        tail = (tail + 1) % TTY_BUF_SIZE;

        if (c == '\n') print_prompt();
    }
}
