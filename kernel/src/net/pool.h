#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct pool_node {
    struct pool_node *next;
} pool_node_t;

typedef struct {
    pool_node_t *head;
} pool_t;

void pool_init(pool_t *p, void *mem, size_t size, uint32_t num);

void *pool_alloc(pool_t *p); // NULL if empty

void pool_free(pool_t *p, void *e);
