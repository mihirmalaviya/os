#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct timer {
    struct timer *next, *prev;
    uint64_t deadline; // ms
    void (*fired)(struct timer *t); // called on expiry
    uint32_t slot;
    bool armed; // currently scheduled - lets a caller avoid double-arming
} timer_t;

// arms t for now+delay_ms
void timer_arm(timer_t *t, uint64_t delay_ms, void (*fired)(timer_t *t));

// unlinks t before it fires
void timer_cancel(timer_t *t);

// advances the wheel by one slot and hands anything due off to timer_task.
// called from irq context (the PIT handler) - never runs a callback itself
void timer_wheel_tick(void);

// creates the task that actually runs fired() callbacks, off the wheel's irq context
void timer_init(void);
