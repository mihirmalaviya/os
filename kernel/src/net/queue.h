#pragma once
#include "net/block.h"
#include <stdint.h>

// a byte stream backed by a chain of blocks
typedef struct {
    block_t *head, *tail;
    uint32_t len; // allocated bytes across the whole chain
    uint32_t dlen; // data bytes actually queued
    uint32_t limit; // max len
} queue_t;

void qinit(queue_t *q, uint32_t limit);

uint32_t qlen(queue_t *q); // data bytes queued (dlen)

int qwrite(queue_t *q, const void *data, uint32_t n); // append to the tail

void qpeek(queue_t *q, void *dst, uint32_t n); // copy front n bytes out, dont remove them

block_t *qcopy(queue_t *q, uint32_t n, uint32_t offset); // peek n bytes at offset, new block w/ headroom reserved

void qdiscard(queue_t *q, uint32_t n); // drop n bytes off the front

void qpass(queue_t *q, block_t *b); // hand a block to the queue with no copy

void qflush(queue_t *q); // free everything
