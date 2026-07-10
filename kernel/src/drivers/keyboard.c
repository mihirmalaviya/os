#include "drivers/keyboard.h"
#include "arch/io.h"
#include "tty/tty.h"
#include <stdint.h>
#include <stdbool.h>

#define SC_MAX 57

static bool shift_held = false;
static bool ctrl_held = false;

// scan code set 1, index == make code
static const key_code_t scancode_to_keycode[] = {
    KEY_UNKNOWN, KEY_ESCAPE,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, KEY_TAB,
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
    KEY_LBRACKET, KEY_RBRACKET, KEY_RETURN, KEY_LCTRL,
    KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
    KEY_SEMICOLON, KEY_QUOTE, KEY_GRAVE, KEY_LSHIFT, KEY_BACKSLASH,
    KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M,
    KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_RSHIFT, KEY_UNKNOWN,
    KEY_UNKNOWN, KEY_SPACE
};

// only entries that don't follow the simple +/-32 letter-case trick
static char shifted_symbol(char c) {
    switch (c) {
        case '1': return '!';
        case '2': return '@';
        case '3': return '#';
        case '4': return '$';
        case '5': return '%';
        case '6': return '^';
        case '7': return '&';
        case '8': return '*';
        case '9': return '(';
        case '0': return ')';
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case ';': return ':';
        case '\'': return '"';
        case '`': return '~';
        case '\\': return '|';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        default:  return c;
    }
}

static char keycode_to_ascii(key_code_t key) {
    if (key >= 0x100) return 0; // non-ascii key (escape, shift, capslock, etc.)

    char c = (char)key;

    if (shift_held) {
        if (c >= 'a' && c <= 'z') return c - 32;
        return shifted_symbol(c);
    }

    return c;
}

void keyboard_handle_irq(void) {
    uint8_t scancode = inb(0x60);

    bool released = scancode & 0x80;
    if (released) scancode &= 0x7F;

    if (scancode > SC_MAX) return;

    key_code_t key = scancode_to_keycode[scancode];

    if (key == KEY_LSHIFT || key == KEY_RSHIFT) {
        shift_held = !released;
        return;
    }
    if (key == KEY_LCTRL) {
        ctrl_held = !released;
        return;
    }

    if (released) return;

    if (ctrl_held && key == KEY_L) {
        tty_enqueue(0x0C); // ASCII form feed, traditional ^L clear-screen
        return;
    }

    char c = keycode_to_ascii(key);
    if (c != 0) {
        tty_enqueue(c);
    }
}
