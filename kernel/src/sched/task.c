#include "sched/task.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "arch/gdt.h"

thread_control_block_t *current_tcb;

static int IRQ_disable_counter = 0;

void lock_scheduler(void) {
    asm volatile ("cli");
    IRQ_disable_counter++;
}

void unlock_scheduler(void) {
    IRQ_disable_counter--;
    if (IRQ_disable_counter == 0) {
        asm volatile ("sti");
    }
}

static int postpone_task_switches_counter = 0;
static int task_switches_postponed_flag = 0;

void lock_stuff(void) {
    asm volatile ("cli");
    IRQ_disable_counter++;
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
    IRQ_disable_counter--;
    if (IRQ_disable_counter == 0) {
        asm volatile ("sti");
    }
}

void sched_init(void) {
    thread_control_block_t *boot = kmalloc(sizeof(thread_control_block_t));
    boot->cr3   = read_cr3();
    boot->rsp0  = *tss_rsp0_ptr;
    boot->state = TCB_RUNNING;
    boot->next  = boot;
    current_tcb = boot;
}

#define TASK_STACK_SIZE (16 * 1024)

thread_control_block_t *first_ready_task;
thread_control_block_t *last_ready_task;

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
		// switch_to_task(current_tcb->next);

		if (postpone_task_switches_counter != 0) {
				task_switches_postponed_flag = 1;
				return;
		}

		if(first_ready_task == NULL) return;

		thread_control_block_t *task = first_ready_task;
		first_ready_task = task->next;
		if (first_ready_task == NULL) last_ready_task = NULL;
		switch_to_task(task);
}

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

    next->state = TCB_RUNNING;
    context_switch(next);
}

void block_task(int reason) {
    lock_scheduler();
    current_tcb->state = reason;
    schedule();
    unlock_scheduler();
}

void unblock_task(thread_control_block_t *task) {
    lock_scheduler();
    if(first_ready_task == NULL) { // Only one task was running before, so pre-empt
        switch_to_task(task);
    } else { // There's at least one task on the "ready to run" queue already, so don't pre-empt
        first_ready_task->next = task;
        first_ready_task = task;
    }
    unlock_scheduler();
}

