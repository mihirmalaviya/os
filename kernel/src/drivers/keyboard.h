#pragma once

// scancode set 1 -> keycode
typedef enum {
    KEY_UNKNOWN   = 0,

    KEY_ESCAPE    = 0x100,
    KEY_LSHIFT    = 0x101,
    KEY_RSHIFT    = 0x102,
    KEY_CAPSLOCK  = 0x103,
    KEY_LCTRL     = 0x104,

    KEY_BACKSPACE = '\b',
    KEY_TAB       = '\t',
    KEY_RETURN    = '\n',
    KEY_SPACE     = ' ',

    KEY_0 = '0', KEY_1 = '1', KEY_2 = '2', KEY_3 = '3', KEY_4 = '4',
    KEY_5 = '5', KEY_6 = '6', KEY_7 = '7', KEY_8 = '8', KEY_9 = '9',

    KEY_A = 'a', KEY_B = 'b', KEY_C = 'c', KEY_D = 'd', KEY_E = 'e',
    KEY_F = 'f', KEY_G = 'g', KEY_H = 'h', KEY_I = 'i', KEY_J = 'j',
    KEY_K = 'k', KEY_L = 'l', KEY_M = 'm', KEY_N = 'n', KEY_O = 'o',
    KEY_P = 'p', KEY_Q = 'q', KEY_R = 'r', KEY_S = 's', KEY_T = 't',
    KEY_U = 'u', KEY_V = 'v', KEY_W = 'w', KEY_X = 'x', KEY_Y = 'y',
    KEY_Z = 'z',

    KEY_MINUS     = '-', KEY_EQUAL     = '=',
    KEY_LBRACKET  = '[', KEY_RBRACKET  = ']',
    KEY_SEMICOLON = ';', KEY_QUOTE     = '\'',
    KEY_GRAVE     = '`', KEY_BACKSLASH = '\\',
    KEY_COMMA     = ',', KEY_DOT       = '.', KEY_SLASH = '/'
} key_code_t;

void keyboard_handle_irq(void);
void keyboard_irq_handler(void *ctx);
void keyboard_init(void);
