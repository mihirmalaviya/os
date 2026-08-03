#include "drivers/e9.h"
#include "arch/io.h"

#define E9_PORT 0xE9

// qemu's debugcon. bytes written here go wherever -debugcon points (file, stdio).
// with no -debugcon flag the port is unclaimed and writes are silently discarded,
// so this is always safe to call. emulator only - does nothing on real hardware.
void e9_putc(char c) {
    outb(E9_PORT, (uint8_t)c);
}
