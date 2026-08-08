#include "net/timer.h"
#include "arch/pit.h"
#include "arch/irq.h"
#include "sched/task.h"
#include <stddef.h>

// 8192 slots * 1ms = 8.192s of direct coverage
// 8192 * sizeof(ptr) = 64kb
#define WHEEL_SIZE 8192

static timer_t *wheel[WHEEL_SIZE]; // each slot is the head of a DLL
static uint32_t wheel_curr; // current time

// q of timers ready to run
static timer_t *pending_head, *pending_tail;
static thread_control_block_t *timer_task_tcb;

static void timer_task(void);

// appends a whole batch to pending q
// caller must hold whatever lock protects pending
static void pending_push(timer_t *batch) {
    if (batch==NULL)
        return;

    timer_t *tail = batch;
    tail->state=TIMER_PENDING; // off the wheel now, about to fire or get rescheduled
    while (tail->next!=NULL) {
        tail=tail->next;
        tail->state=TIMER_PENDING;
    }

    if (pending_tail!=NULL)
        pending_tail->next=batch;
    else
        pending_head=batch;
    pending_tail=tail;
}

// takes everything off pending q
// caller must hold whatever lock protects pending
static timer_t *pending_pop(void) {
    timer_t *batch = pending_head;
    pending_head=pending_tail=NULL;
    return batch;
}

void timer_arm(timer_t *t, uint64_t delay_ms, void (*fired)(timer_t *t)) {
    timer_cancel(t);

    t->deadline=now_ms()+delay_ms;
    t->fired=fired;
    t->state=TIMER_ARMED;

    uint32_t slot = (uint32_t)((wheel_curr+delay_ms) % WHEEL_SIZE);
    t->slot=slot;

    uint64_t flags = irq_save();
    t->prev=NULL;
    t->next=wheel[slot];
    if (wheel[slot]!=NULL)
        wheel[slot]->prev=t;
    wheel[slot]=t;
    irq_restore(flags);
}

void timer_cancel(timer_t *t) {
    uint64_t flags = irq_save();

    // only unlink if its actually sitting in the wheel
    if (t->state==TIMER_ARMED) {
        if (t->prev!=NULL)
            t->prev->next=t->next;
        else
            wheel[t->slot]=t->next;
        if (t->next!=NULL)
            t->next->prev=t->prev;
    }

    irq_restore(flags);

    t->state=TIMER_OFF;
}

// called from irq context
void timer_wheel_tick(void) {
    uint64_t flags = irq_save();

    wheel_curr = (wheel_curr+1)%WHEEL_SIZE;
    timer_t *curr = wheel[wheel_curr];
    wheel[wheel_curr] = NULL;

    pending_push(curr);

    irq_restore(flags);

    if (curr!=NULL && timer_task_tcb)
        unblock_task_from_irq(timer_task_tcb);
}

static void timer_task(void) {
    unlock_scheduler();

    for (;;) {
        lock_stuff();
        if (pending_head==NULL)
            block_task(TCB_BLOCKED);

        timer_t *curr = pending_pop();
        unlock_stuff();

        while (curr!=NULL) {
            timer_t *next = curr->next;
            if (curr->state==TIMER_PENDING) {
                if (curr->deadline <= now_ms()) {
                    curr->fired(curr); // fire stuff if its time
                } else {
                    timer_arm(curr, curr->deadline - now_ms(), curr->fired); // reschedule the rest
                }
            }
            curr=next;
        }
    }
}

void timer_init(void) {
    timer_task_tcb = task_create_prio(timer_task, PRIO_HIGH);
}
