#include "net/block.h"
#include "arch/irq.h"
#include "mm/dma.h"
#include "kernel.h"
#include <stddef.h>

#define PAGE_SIZE 4096ULL
#define BLOCK_PER_PAGE ((int)(PAGE_SIZE/BLOCK_SIZE))

_Static_assert(PAGE_SIZE % BLOCK_SIZE == 0, "blocks must divide a page evenly");
_Static_assert(BLOCK_NUM % BLOCK_PER_PAGE == 0, "block count must fill whole pages");

static block_t blocks[BLOCK_NUM];
static block_t *free_head;
static uint32_t nfree;

void block_init(void) {
    ASSERT(free_head==NULL, "called init twice");

    nfree=0;
    for (int i=0; i<BLOCK_NUM/BLOCK_PER_PAGE; i++) {
        dma_buf_t pg = dma_alloc(1);
        ASSERT(pg.virt != NULL); // boot time, and ram is not a constraint

        for (int j=0; j<BLOCK_PER_PAGE; j++) {
            block_t *b = &blocks[i*BLOCK_PER_PAGE + j];
            b->head = (uint8_t *)pg.virt + j*BLOCK_SIZE;
            b->end = b->head + BLOCK_SIZE;
            b->in_use = false;
            b->next = free_head;
            free_head = b;
            nfree++;
        }
    }
}

block_t *block_alloc(uint32_t size, uint32_t headroom) {
    if (size+headroom > BLOCK_SIZE)
        return NULL;

    uint64_t flags = irq_save();
    block_t *b = free_head; // pop the head
    if (b!=NULL) {
        free_head = b->next;
        nfree--;
    }
    irq_restore(flags);

    if (b==NULL) // out of blocks, caller drops
        return NULL;

    ASSERT(!b->in_use); // a block on the freelist was handed out twice
    b->in_use=true;
    b->next=NULL;
    b->data=b->head+headroom;
    b->tail=b->data;
    return b;
}

void block_free(block_t *b) {
    if (b==NULL)
        return;

    ASSERT(b->in_use); // freed twice
    b->in_use = false;

    uint64_t flags=irq_save();
    b->next=free_head;
    free_head=b;
    nfree++;
    irq_restore(flags);
}

// bytes of payload
uint32_t block_len(const block_t *b) {
    return (uint32_t)(b->tail - b->data);
}

// bytes free after the payload
uint32_t block_room(const block_t *b) {
    return (uint32_t)(b->end - b->tail);
}

// walk data back n bytes into the headroom, returns where to write the header (tx)
uint8_t *block_push(block_t *b, uint32_t n) {
    ASSERT((uint32_t)(b->data - b->head) >= n); // not enough headroom was reserved
    b->data -= n;
    return b->data;
}

// walk past header
uint8_t *block_pull(block_t *b, uint32_t n) {
    ASSERT(n <= block_len(b));
    b->data += n;
    return b->data;
}

uint8_t *block_put(block_t *b, uint32_t n) {
    ASSERT(n <= block_room(b));
    uint8_t *old = b->tail;
    b->tail += n;
    return old;
}

// reserve headroom
void block_reserve(block_t *b, uint32_t n) {
    ASSERT(block_len(b)==0);
    ASSERT(n <= (uint32_t)(b->end - b->head));
    b->data = b->head+n;
    b->tail = b->data;
}

// drop trailing bytes a header's own length field says arent real payload
void block_trim(block_t *b, uint32_t n) {
    ASSERT(n <= block_len(b)); // trim only removes bytes, never adds them
    b->tail = b->data + n;
}

// # of free blocks
uint32_t block_nfree(void) {
    return nfree;
}
