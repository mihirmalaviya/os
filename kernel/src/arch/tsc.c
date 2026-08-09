#include "arch/tsc.h"
#include "arch/io.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_FREQ 1193182
#define PIT_RELOAD (PIT_BASE_FREQ/1000) // matches pit_inits divisor at 1000hz

#define CALIBRATE_MS 50

static uint64_t tsc_per_ms;

static uint16_t pit_read_count(void) {
    outb(PIT_COMMAND, 0x00); // latch channel 0s current count
    uint8_t lo = inb(PIT_CHANNEL0);
    uint8_t hi = inb(PIT_CHANNEL0);
    return (uint16_t)(lo | (hi<<8));
}

// must run before sti()
void tsc_calibrate(void) {
    uint16_t prev = pit_read_count();
    uint64_t tsc_start = rdtsc();

    uint32_t elapsed_pit_ticks = 0;
    const uint32_t target = PIT_RELOAD*CALIBRATE_MS;

    while (elapsed_pit_ticks < target) {
        uint16_t curr = pit_read_count();
        uint16_t delta = (uint16_t)(prev-curr); // wraps correctly across a reload
        elapsed_pit_ticks += delta;
        prev = curr;
    }

    uint64_t tsc_delta = rdtsc()-tsc_start;
    tsc_per_ms = tsc_delta/CALIBRATE_MS;
}

uint64_t tsc_ms(void) {
    return rdtsc()/tsc_per_ms;
}
