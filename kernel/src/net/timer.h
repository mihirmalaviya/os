#pragma once
#include <stdint.h>

typedef enum {
    TIMER_OFF=0, // not scheduled
    TIMER_ARMED, // sitting in the wheel
    TIMER_PENDING, // handed off to timer_task, about to fire/rescheduled
} timer_state_t;

typedef struct timer {
    struct timer *next, *prev;
    uint64_t deadline; // ms
    void (*fired)(struct timer *t); // called on expiry
    uint32_t slot;
    timer_state_t state;
} timer_t;

// arms t for now+delay_ms
void timer_arm(timer_t *t, uint64_t delay_ms, void (*fired)(timer_t *t));

// unlinks t before it fires
void timer_cancel(timer_t *t);

// advances the wheel by one slot and hands anything due off to timer_task
// called from irq context (the PIT handler)
void timer_wheel_tick(void);

// creates the task that actually runs fired() callbacks, off the wheels irq context
void timer_init(void);
