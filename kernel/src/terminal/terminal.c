#include "terminal.h"
#include "string.h"
#include <stdarg.h>
#include <nanoprintf.h>

extern int scanline;
extern uint64_t fb_size;
extern char *fb;

#define MAX_COLS (scanline / ((psf_glyph_width() + 1) * 4))
#define MAX_ROWS ((fb_size / scanline) / psf_glyph_height())

static int cursor_x = 0;
static int cursor_y = 0;

static void scroll(void) {
    int row_bytes = psf_glyph_height() * scanline;
    memmove(fb, fb + row_bytes, fb_size - row_bytes);
    memset(fb + fb_size - row_bytes, 0, row_bytes);
}

static void print_char(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= MAX_ROWS) {
            scroll();
            cursor_y = MAX_ROWS - 1;
        }
        return;
    }

    if (cursor_x >= MAX_COLS) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= MAX_ROWS) {
            scroll();
            cursor_y = MAX_ROWS - 1;
        }
    }

    putchar((unsigned short int)c, cursor_x, cursor_y, 0xFFFFFF, 0x000000);
    cursor_x++;
}

void kputchar(char c) {
    print_char(c);
}

void kbackspace(void) {
    if (cursor_x == 0) return; // not walking back across lines, keep it simple
    cursor_x--;
    putchar((unsigned short int)' ', cursor_x, cursor_y, 0xFFFFFF, 0x000000);
}

void terminal_clear(void) {
    memset(fb, 0, fb_size);
    cursor_x = 0;
    cursor_y = 0;
}

#define KPRINTF_BUF_SIZE 1024

void kprintf(const char *fmt, ...) {
    char buf[KPRINTF_BUF_SIZE];
    va_list args;

    va_start(args, fmt);
    int len = npf_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len < 0) {
        return;
    }
    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1; // npf_vsnprintf already truncated + null-terminated buf for us
    }

    for (int i = 0; i < len; i++) {
        print_char(buf[i]);
    }
}
