#include "terminal.h"
#include "memory.h"
#include <stdarg.h>

extern int scanline;
extern uint64_t fb_size;
extern char *fb;

#define MAX_COLS (scanline / 4 / 8)
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

void terminal_clear(void) {
    memset(fb, 0, fb_size);
    cursor_x = 0;
    cursor_y = 0;
}

static void print_str(const char *s) {
    while (*s != '\0') {
        print_char(*s);
        s++;
    }
}

static void print_hex(uint64_t n) {
    const char *digits = "0123456789abcdef";
    char buf[16];
    int i = 0;
    if (n == 0) { print_char('0'); return; }
    while (n > 0) {
        buf[i++] = digits[n & 0xf];
        n >>= 4;
    }
    for (int j = i - 1; j >= 0; j--)
        print_char(buf[j]);
}

static void print_int(int n) {
    if (n < 0) {
        print_char('-');
        n = -n;
    }
    if (n == 0) {
        print_char('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        print_char(buf[j]);
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt != '\0') {
        if (*fmt != '%') {
            print_char(*fmt);
        } else {
            fmt++;
            switch (*fmt) {
                case 's': print_str(va_arg(args, char *));      break;
                case 'd': print_int(va_arg(args, int));         break;
                case 'x': print_hex(va_arg(args, uint64_t));    break;
                case '%': print_char('%');                      break;
            }
        }
        fmt++;
    }
    va_end(args);
}
