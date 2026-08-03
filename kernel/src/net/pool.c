#include "net/pool.h"
#include "arch/irq.h"

void pool_init(pool_t *p, void *mem, size_t size, uint32_t num) {
    p->head = NULL;

    uint8_t *pool_mem = mem;
    for (uint32_t i=num; i>0; i--) {
        pool_node_t *e = (pool_node_t *)(pool_mem + (i-1)*size);
        e->next = p->head;
        p->head = e;
    }
}

void *pool_alloc(pool_t *p) {
    uint64_t flags = irq_save();
    pool_node_t *e = p->head; // pop the head
    if (e!=NULL)
        p->head = e->next;
    irq_restore(flags);
    return e; // NULL if the pool was empty
}

void pool_free(pool_t *p, void *e) {
    pool_node_t *n = e;
    uint64_t flags = irq_save();
    n->next = p->head;
    p->head = n;
    irq_restore(flags);
}
