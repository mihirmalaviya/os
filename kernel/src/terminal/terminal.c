#include "terminal.h"
#include "string.h"
#include "mm/heap.h"
#include <stdarg.h>
#include <nanoprintf.h>

extern int scanline;
extern uint64_t fb_size;
extern char *fb;

#define MAX_COLS (scanline / ((psf_glyph_width() + 1) * 4))
#define MAX_ROWS ((fb_size / scanline) / psf_glyph_height())

static int cursor_x = 0;
static int cursor_y = 0;

// Ring buffer of text lines. We never read the framebuffer: normal characters
// are drawn straight to their fixed screen position, and on scroll we advance
// `ring_top` (so no line data is copied) and repaint the whole screen from the
// ring — a write-only pass. `grid` is NULL until terminal_init() runs after the
// heap is up; before that we fall back to the old read-from-fb scroll.
static char *grid;      // rows * cols cells, indexed by physical ring row
static int t_rows;
static int t_cols;
static int ring_top;    // ring index that is currently the topmost visible row

static inline int ring_row(int visible_y) {
    return (ring_top + visible_y) % t_rows;
}

static inline char *cell(int visible_y, int x) {
    return &grid[ring_row(visible_y) * t_cols + x];
}

void terminal_init(void) {
    t_rows = MAX_ROWS;
    t_cols = MAX_COLS;
    grid = kmalloc((size_t)t_rows * t_cols);
    memset(grid, 0, (size_t)t_rows * t_cols);
    ring_top = 0;
}

// redraw every visible cell from the ring buffer (write-only, no fb reads)
static void repaint(void) {
    for (int y = 0; y < t_rows; y++) {
        for (int x = 0; x < t_cols; x++) {
            char c = *cell(y, x);
            putchar((unsigned short int)(c ? c : ' '), x, y, 0xFFFFFF, 0x000000);
        }
    }
}

static void scroll(void) {
    if (!grid) {
        // pre-heap fallback: the only path that still reads the framebuffer
        int row_bytes = psf_glyph_height() * scanline;
        memmove(fb, fb + row_bytes, fb_size - row_bytes);
        memset(fb + fb_size - row_bytes, 0, row_bytes);
        return;
    }
    // advance the window by one line (no line data moves) and clear the new
    // bottom row, then repaint the screen from the ring.
    ring_top = (ring_top + 1) % t_rows;
    memset(cell(t_rows - 1, 0), 0, t_cols);
    repaint();
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

    if (grid) *cell(cursor_y, cursor_x) = c;
    putchar((unsigned short int)c, cursor_x, cursor_y, 0xFFFFFF, 0x000000);
    cursor_x++;
}

void kputchar(char c) {
    print_char(c);
}

void kbackspace(void) {
    if (cursor_x == 0) return; // not walking back across lines, keep it simple
    cursor_x--;
    if (grid) *cell(cursor_y, cursor_x) = 0;
    putchar((unsigned short int)' ', cursor_x, cursor_y, 0xFFFFFF, 0x000000);
}

void terminal_clear(void) {
    if (grid) {
        memset(grid, 0, (size_t)t_rows * t_cols);
        ring_top = 0;
    }
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
