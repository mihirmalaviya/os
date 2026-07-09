#include "arch/pit.h"
#include "arch/io.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182

#define PIT_FREQ_HZ 1000

static volatile uint64_t timer_ticks = 0;
static uint64_t ns_per_tick = 1000000000ULL / PIT_FREQ_HZ;

uint64_t time_since_boot = 0;
void pit_init() {
    uint16_t divisor = PIT_BASE_FREQ / PIT_FREQ_HZ;
    outb(PIT_COMMAND, 0x36); // channel 0, lobyte/hibyte, mode 3 (square wave)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void pit_tick(void) {
    timer_ticks++;
}

uint64_t get_time_since_boot(void) {
    return timer_ticks * ns_per_tick;
}

void sleep_ticks(uint64_t ticks) {
    uint64_t target = timer_ticks + ticks;
    while (timer_ticks < target) {
        asm volatile ("hlt");
    }
}
