#pragma once

// prints the initial prompt; call once at boot before the main loop
void tty_init(void);

// called from the keyboard IRQ handler to hand off a resolved character
void tty_enqueue(char c);

// clears the screen, discards anything pending in the buffer, and
// reprints the prompt
void tty_clear(void);

// called from the main loop (outside interrupt context) to drain
// whatever's been queued up and actually print it
void tty_poll(void);
