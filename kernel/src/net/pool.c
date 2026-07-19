#include "net/pool.h"
#include "arch/irq.h"
#include <stdint.h>
#include <stddef.h>

typedef struct pool_node {
    struct pool_node *next;
} pool_node_t;

static uint8_t      pool_mem[POOL_NUM*POOL_SIZE];
static pool_node_t *head;

void pool_init(void) {
    head = NULL;
    for (int i=0; i<POOL_NUM; i++) {
        pool_node_t *e = (pool_node_t *)(pool_mem + i*POOL_SIZE);
        e->next = head;
        head = e;
    }
}

void *pool_alloc(void) {
    uint64_t flags = irq_save();
    pool_node_t *e = head; // pop the head
    if (e!=NULL) {
        head = e->next;
    }
    irq_restore(flags);
    return e; // NULL if the pool was empty
}

void pool_free(void *p) {
    if (p==NULL) return;
    pool_node_t *e = (pool_node_t *)p;

    uint64_t flags = irq_save();
    e->next = head;
    head = e;
    irq_restore(flags);
}
