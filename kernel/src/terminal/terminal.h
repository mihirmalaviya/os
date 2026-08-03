#include <stdarg.h>
#include "psf.h"

void terminal_init(void);

// formats into a buffer, then pushes it one char at a time to putc_fn
int kprintf_to(void (*putc_fn)(char), const char *fmt, va_list args);

__attribute__((format(printf, 1, 2))) void kprintf(const char *fmt, ...);  // framebuffer
__attribute__((format(printf, 1, 2))) void debugf(const char *fmt, ...);   // qemu debugcon (0xE9)
__attribute__((format(printf, 1, 2))) void mprintf(const char *fmt, ...);  // both
void kputchar(char c);
void kbackspace(void);
void terminal_clear(void);
