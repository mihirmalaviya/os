#include "net/queue.h"
#include "kernel.h"
#include "lib/string.h"
#include <stddef.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

void qinit(queue_t *q, uint32_t limit) {
    q->head=q->tail=NULL;
    q->limit=limit;
    q->len=0;
    q->dlen=0;
}

uint32_t qlen(queue_t *q) {
    return q->dlen;
}

int qwrite(queue_t *q, const void *data, uint32_t n) {
    const uint8_t *src = data; // pointer for data
    uint32_t written=0;
    while (written<n){
        if (q->tail==NULL || block_room(q->tail)==0){ // we need to add a block
            if (q->len+BLOCK_SIZE > q->limit) // is it within budget?
                return written;
            block_t *b=block_alloc(BLOCK_SIZE, 0);
            if (b==NULL)
                return written;

            // add it
            if (q->head==NULL){
                q->head=q->tail=b;
            } else {
                q->tail->next=b;
                q->tail=b;
            }
            q->len+=BLOCK_SIZE;
        }
        uint32_t room = block_room(q->tail);
        uint32_t take = MIN(room, n-written);
        uint8_t *dst = block_put(q->tail, take);
        memcpy(dst, src+written, take);

        q->dlen+=take;
        written+=take;
    }
    return written;
}

void qpeek(queue_t *q, void *dst, uint32_t n) {
    ASSERT(n <= q->dlen);

    uint8_t *out = dst;
    uint32_t copied = 0;
    block_t *b = q->head;
    while (copied<n) {
        uint32_t take = MIN(block_len(b), n-copied);
        memcpy(out+copied, b->data, take);
        copied += take;
        b = b->next;
    }
}

block_t *qcopy(queue_t *q, uint32_t n, uint32_t offset) {
    ASSERT(offset+n <= q->dlen);

    block_t *out = block_alloc(n, HDR_TCP);
    if (out==NULL)
        return NULL;

    // skip phase: walk to the block that offset lands in
    block_t *b = q->head;
    uint32_t skip = offset;
    while (b!=NULL && skip >= block_len(b)) {
        skip -= block_len(b);
        b = b->next;
    }

    // copy phase: skip only applies to the first block, then resets to 0
    uint32_t copied = 0;
    while (copied < n) {
        uint32_t take = MIN(block_len(b)-skip, n-copied);
        uint8_t *dst = block_put(out, take);
        memcpy(dst, b->data+skip, take);

        copied += take;
        skip = 0;
        b = b->next;
    }

    return out;
}

void qdiscard(queue_t *q, uint32_t n) {
    ASSERT(n <= q->dlen, "tried to discard too much");

    uint32_t left = n;
    while (left>0) {
        uint32_t len = block_len(q->head);
        if (left<len) {
            block_pull(q->head, left);
            left=0;
        } else {
            block_t *b = q->head;
            q->head = b->next;
            if (q->head==NULL)
                q->tail=NULL;
            left-=len;
            q->len -= BLOCK_SIZE;
            block_free(b);
        }
    }
    q->dlen -= n;
}

// hand a block to the queue with no copy
void qpass(queue_t *q, block_t *b) {
    b->next=NULL;
    if (q->tail!=NULL)
        q->tail->next = b;
    else
        q->head = b;
    q->tail = b;

    q->len += BLOCK_SIZE;
    q->dlen += block_len(b);
}

void qflush(queue_t *q) {
    block_t *b = q->head;
    while (b!=NULL) {
        block_t *next = b->next;
        block_free(b);
        b=next;
    }
    q->head = q->tail = NULL;
    q->len=0;
    q->dlen=0;
}
