#include "sched/task.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "arch/gdt.h"
#include "arch/pit.h"
#include "terminal/terminal.h"

thread_control_block_t *current_tcb;
thread_control_block_t *idle_task;

void lock_scheduler(void) {
    asm volatile ("cli");
    current_tcb->irq_disable_counter++;
}

void unlock_scheduler(void) {
    current_tcb->irq_disable_counter--;
    if (current_tcb->irq_disable_counter == 0) {
        asm volatile ("sti");
    }
}

static int postpone_task_switches_counter = 0;
static int task_switches_postponed_flag = 0;

void lock_stuff(void) {
    asm volatile ("cli");
    current_tcb->irq_disable_counter++;
    postpone_task_switches_counter++;
}

void unlock_stuff(void) {
    postpone_task_switches_counter--;
    if (postpone_task_switches_counter == 0) {
        if (task_switches_postponed_flag != 0) {
            task_switches_postponed_flag = 0;
            schedule();
        }
    }
    current_tcb->irq_disable_counter--;
    if (current_tcb->irq_disable_counter == 0) {
        asm volatile ("sti");
    }
}

// these track when it is safe to switch tasks
void postpone_switches(void) {
    postpone_task_switches_counter++;
}

void unpostpone_switches(void) {
    postpone_task_switches_counter--;
}

// called in isr handler
void check_postponed_switch(void) {
    if (postpone_task_switches_counter == 0 && task_switches_postponed_flag) {
        task_switches_postponed_flag = 0;
        schedule();
    }
}

#define TASK_STACK_SIZE (16 * 1024)
#define TIME_SLICE_LENGTH 50000000   // 50000000 nanoseconds is 50 ms

thread_control_block_t *first_ready_task;
thread_control_block_t *last_ready_task;

thread_control_block_t *first_sleeping_task;

thread_control_block_t *first_terminated_task;

static uint64_t next_task_id = 0;

static const char *tcb_state_name(tcb_state_t state) {
    switch (state) {
        case TCB_READY:            return "ready";
        case TCB_RUNNING:          return "running";
        case TCB_SLEEPING:         return "sleeping";
        case TCB_BLOCKED:          return "blocked";
        case TCB_PAUSED:           return "paused";
        case TCB_WAITING_FOR_LOCK: return "waiting_for_lock";
        case TCB_TERMINATED:       return "terminated";
        default:                   return "unknown";
    }
}

static void print_task_list(thread_control_block_t *head) {
    for (thread_control_block_t *task = head; task != NULL; task = task->next) {
        kprintf("  task %d: ticks_used=%d %s\n", (int)task->task_id, (int)task->ticks_used, tcb_state_name(task->state));
    }
}

void print_tasks(void) {
    // kprintf("task %d: %s (current)\n", (int)current_tcb->task_id, tcb_state_name(current_tcb->state));
    print_task_list(first_ready_task);
    print_task_list(first_sleeping_task);
    print_task_list(first_terminated_task);
}

thread_control_block_t *task_create(void (*entry)(void)) {
    thread_control_block_t *tcb = kmalloc(sizeof(thread_control_block_t)); // pointer to tcb in heap
    uint8_t *stack = kmalloc(TASK_STACK_SIZE); // pointer to stack in heap
    uint64_t *sp = (uint64_t *)(stack + TASK_STACK_SIZE) - 7; // -7 because we about to allocate 7 thingys

    sp[6] = (uint64_t)entry; // return address switch_to_task's `ret` will pop
    sp[5] = 0; // rbx
    sp[4] = 0; // rbp
    sp[3] = 0; // r12
    sp[2] = 0; // r13
    sp[1] = 0; // r14
    sp[0] = 0; // r15

    tcb->rsp   = (uint64_t)sp;
    tcb->rsp0  = (uint64_t)(stack + TASK_STACK_SIZE);
    tcb->cr3   = read_cr3();
    tcb->state = TCB_READY;
    tcb->time_slice_length = TIME_SLICE_LENGTH;
    tcb->irq_disable_counter = 1;
    tcb->task_id = next_task_id++;
		// tcb->next = current_tcb->next;
		// current_tcb->next = tcb;

		if (last_ready_task == NULL) {
				first_ready_task = tcb;
				last_ready_task  = tcb;
		} else {
				last_ready_task->next = tcb;
				last_ready_task = tcb;
		}

    return tcb;
}


void schedule() {
    if (postpone_task_switches_counter != 0) {
        task_switches_postponed_flag = 1;
        return;
    }
    if (first_ready_task != NULL) {
        thread_control_block_t *task = first_ready_task;
        first_ready_task = task->next;
        if (first_ready_task == NULL) last_ready_task = NULL;

        if (task == idle_task) {
            // Try to find an alternative to prevent the idle task getting CPU time
            if (first_ready_task != NULL) {
                // Idle task was selected but other tasks are "ready to run"
                task = first_ready_task;
                first_ready_task = task->next;
                if (first_ready_task == NULL) last_ready_task = NULL;

                idle_task->next = NULL;
                if (last_ready_task == NULL) {
                    first_ready_task = idle_task;
                    last_ready_task  = idle_task;
                } else {
                    last_ready_task->next = idle_task;
                    last_ready_task = idle_task;
                }
            } else if (current_tcb->state == TCB_RUNNING) {
                // No other tasks ready to run, but the currently running task wasn't blocked and can keep running
                first_ready_task = idle_task;
                last_ready_task  = idle_task;
                idle_task->next  = NULL;
                return;
            } else {
                // No other options - the idle task is the only task that can be given CPU time
            }
        }
        switch_to_task(task);
    }
}

uint64_t time_slice_remaining = 0;

uint64_t time_between_ticks = 1000000;

void switch_to_task(thread_control_block_t *next) {
    if (postpone_task_switches_counter != 0) {
        task_switches_postponed_flag = 1;
        return;
    }

    if (current_tcb->state == TCB_RUNNING) { // if it isnt running dont add it back to the queue
        current_tcb->state = TCB_READY;
        current_tcb->next = NULL;
        if (last_ready_task == NULL) { // if there are no other tasks
            first_ready_task = current_tcb;
            last_ready_task  = current_tcb;
        } else {
            last_ready_task->next = current_tcb;
            last_ready_task = current_tcb;
        }
    }

    time_slice_remaining = (next == idle_task) ? 0 : next->time_slice_length;

    next->state = TCB_RUNNING;
    context_switch(next);
}

void block_task(int reason) {
    lock_scheduler();
    current_tcb->state = reason;
    schedule();
    unlock_scheduler();
}

// raw
static void __unblock_task(thread_control_block_t *task) {
    task->state = TCB_READY;
    task->next  = NULL;
    if (last_ready_task == NULL) {
        first_ready_task = task;
        last_ready_task  = task;
    } else {
        last_ready_task->next = task;
        last_ready_task = task;
    }

    if (current_tcb == idle_task) {
        schedule();
    }
}

void unblock_task(thread_control_block_t *task) {
    lock_scheduler();
    __unblock_task(task);
    unlock_scheduler();
}


void PIT_IRQ_handler(void) {
    thread_control_block_t *next_task;
    thread_control_block_t *this_task;

    postpone_switches();

    pit_tick();
    current_tcb->ticks_used++;

    // Move everything from the sleeping task list into a temporary variable and make the sleeping task list empty
    next_task = first_sleeping_task;
    first_sleeping_task = NULL;

    // For each task, wake it up or put it back on the sleeping task list
    while (next_task != NULL) {
        this_task = next_task;
        next_task = this_task->next;

        if (this_task->sleep_expiry <= get_time_since_boot()) {
            // Task needs to be woken up
            __unblock_task(this_task);
        } else {
            // Task needs to be put back on the sleeping task list
            this_task->next = first_sleeping_task;
            first_sleeping_task = this_task;
        }
    }

		// Handle "end of time slice" preemption
    if(time_slice_remaining != 0) {
        // There is a time slice length
        if(time_slice_remaining <= time_between_ticks) {
            schedule();
        } else {
            time_slice_remaining -= time_between_ticks;
        }
    }

    unpostpone_switches();
}


void nano_sleep_until(uint64_t when) {
    lock_stuff();

    // Make sure "when" hasn't already occured
    if (when < get_time_since_boot()) {
        unlock_stuff();
        return;
    }

    // Set time when task should wake up
    current_tcb->sleep_expiry = when;

    // Add task to the start of the unsorted list of sleeping tasks
    current_tcb->next = first_sleeping_task;
    first_sleeping_task = current_tcb;

    unlock_stuff();

    // Find something else for the CPU to do
    block_task(TCB_SLEEPING);
}

void nano_sleep(uint64_t nanoseconds) {
    nano_sleep_until(get_time_since_boot() + nanoseconds);
}

void ms_sleep(uint64_t milliseconds) {
    nano_sleep(milliseconds * 1000000ULL);
}

extern thread_control_block_t *cleaner_task_tcb;

void terminate_task(void) {
    // Note: Can do any harmless stuff here (close files, free memory in user-space, ...) but there's none of that yet
    lock_stuff();

    // Put this task on the terminated task list
    lock_scheduler();
    current_tcb->next = first_terminated_task;
    first_terminated_task = current_tcb;
    unlock_scheduler();

    // Block this task (note: task switch will be postponed until scheduler lock is released)
    block_task(TCB_TERMINATED);

    // Make sure the cleaner task isn't paused
    unblock_task(cleaner_task_tcb);

    // Unlock the scheduler's lock
    unlock_stuff();
}

SEMAPHORE *create_semaphore(int max_count) {
    SEMAPHORE * semaphore;

    semaphore = kmalloc(sizeof(SEMAPHORE));
    if(semaphore != NULL) {
        semaphore->max_count = max_count;
        semaphore->current_count = 0;
        semaphore->first_waiting_task = NULL;
        semaphore->last_waiting_task = NULL;
    }
    return semaphore;
}

SEMAPHORE *create_mutex(void) {
    return create_semaphore(1);
}

void acquire_semaphore(SEMAPHORE *semaphore) {
    lock_stuff();
    if(semaphore->current_count < semaphore->max_count) {
        // We can acquire now
        semaphore->current_count++;
    } else {
        // We have to wait
        current_tcb->next = NULL;
        if(semaphore->first_waiting_task == NULL) {
            semaphore->first_waiting_task = current_tcb;
        } else {
            semaphore->last_waiting_task->next = current_tcb;
        }
        semaphore->last_waiting_task = current_tcb;
        block_task(TCB_WAITING_FOR_LOCK);    // This task will be unblocked when it can acquire the semaphore
    }
    unlock_stuff();
}

void acquire_mutex(SEMAPHORE *semaphore) {
    acquire_semaphore(semaphore);
}

void release_semaphore(SEMAPHORE * semaphore) {
    lock_stuff();

    if(semaphore->first_waiting_task != NULL) {
        // We need to wake up the first task that was waiting for the semaphore
        // Note: "semaphore->current_count" remains the same (this task leaves and another task enters)

        thread_control_block_t *task = semaphore->first_waiting_task;
        semaphore->first_waiting_task = task->next;
        unblock_task(task);
    } else {
        // No tasks are waiting
        semaphore->current_count--;
    }
    unlock_stuff();
}

void release_mutex(SEMAPHORE *semaphore) {
    release_semaphore(semaphore);
}

thread_control_block_t *cleaner_task_tcb;

void cleanup_terminated_task(thread_control_block_t *task) {
        kfree((void *)(task->rsp0 - TASK_STACK_SIZE));
        kfree(task);
}

void cleaner_task(void) {
    thread_control_block_t *task;

    unlock_scheduler();

    for (;;) {
        lock_stuff();

        while (first_terminated_task != NULL) {
            task = first_terminated_task;
            first_terminated_task = task->next;
            cleanup_terminated_task(task);
        }

        block_task(TCB_PAUSED);
        unlock_stuff();
    }
}

void kernel_idle_task(void) {
    unlock_scheduler();
    for(;;) {
        asm volatile ("hlt");
    }
}

void sched_init(void) {
    thread_control_block_t *boot = kmalloc(sizeof(thread_control_block_t));
    boot->cr3   = read_cr3();
    boot->rsp0  = *tss_rsp0_ptr;
    boot->state = TCB_RUNNING;
    boot->time_slice_length = TIME_SLICE_LENGTH;
    boot->irq_disable_counter = 0;
    boot->task_id = next_task_id++;
    boot->next  = boot;
    current_tcb = boot;
    time_slice_remaining = boot->time_slice_length;
		idle_task = task_create(kernel_idle_task);
		cleaner_task_tcb = task_create(cleaner_task);
}

