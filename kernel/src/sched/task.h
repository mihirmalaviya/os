#pragma once
#include <stdint.h>

typedef enum {
    TCB_READY,
    TCB_RUNNING,
    TCB_BLOCKED,
    TCB_TERMINATED,
} tcb_state_t;

// TODO: fields will grow as the tutorial steps land (time accounting,
// sleep_expiry, scheduling policy/priority, ...)
typedef struct thread_control_block {
    uint64_t rsp;      // saved kernel stack pointer (switch_to_task reads/writes this)
    uint64_t rsp0;     // top of this task's kernel stack (loaded into TSS.rsp0 on switch)
    uint64_t cr3;      // this task's address space
    struct thread_control_block *next;
    tcb_state_t state;
		uint64_t ticks_used;
} thread_control_block_t;

extern thread_control_block_t *current_tcb;

void sched_init(void);
thread_control_block_t *task_create(void (*entry)(void));
void context_switch(thread_control_block_t *next);

// caller must hold the scheduler lock (lock_scheduler()) before calling
void switch_to_task(thread_control_block_t *next);
// caller must hold the scheduler lock (lock_scheduler()) before calling
void schedule();

void lock_scheduler(void);
void unlock_scheduler(void);

void lock_stuff(void);
void unlock_stuff(void);

void block_task(int reason);
void unblock_task(thread_control_block_t *task);
