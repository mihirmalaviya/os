#pragma once
#include <stdint.h>

typedef enum {
    TCB_READY,
    TCB_RUNNING,
    TCB_SLEEPING,
    TCB_BLOCKED,
    TCB_PAUSED,
    TCB_WAITING_FOR_LOCK,
    TCB_TERMINATED,
} tcb_state_t;

typedef struct thread_control_block {
    uint64_t rsp;      // saved kernel stack pointer (switch_to_task reads/writes this)
    uint64_t rsp0;     // top of this task's kernel stack (loaded into TSS.rsp0 on switch)
    uint64_t cr3;      // this task's address space
    struct thread_control_block *next;
    tcb_state_t state;
    uint64_t task_id; // unique, never reused (unlike the tcb's own address)
    uint64_t ticks_used;
    uint64_t sleep_expiry;
    uint64_t time_slice_length; // ns of CPU time this task gets before forced preemption
    int irq_disable_counter; // nesting depth of this task's own lock_scheduler()/lock_stuff() calls
} thread_control_block_t;

extern thread_control_block_t *current_tcb;

typedef struct {
    int max_count;
    int current_count;
    thread_control_block_t *first_waiting_task;
    thread_control_block_t *last_waiting_task;
} SEMAPHORE;

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

void PIT_IRQ_handler(void *ctx);
void check_postponed_switch(void);

void nano_sleep_until(uint64_t when);
void nano_sleep(uint64_t nanoseconds);
void ms_sleep(uint64_t milliseconds);

void terminate_task(void);
void print_tasks(void);

void semaphore_init(SEMAPHORE *semaphore, int max_count);
void semaphore_acquire(SEMAPHORE *semaphore);
void acquire_mutex(SEMAPHORE *semaphore);
void semaphore_release(SEMAPHORE *semaphore);
void semaphore_release_from_irq(SEMAPHORE *semaphore); // wake a waiter from an irq handler
void release_mutex(SEMAPHORE *semaphore);

void postpone_switches(void);
void unpostpone_switches(void);
