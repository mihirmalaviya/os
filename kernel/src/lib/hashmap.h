#pragma once
#include <stddef.h>

typedef struct hnode {
    struct hnode *next;
    struct hnode *prev;
} hnode_t;

static inline void hnode_insert(hnode_t **bucket, hnode_t *n) {
    n->prev = NULL;
    n->next = *bucket;
    if (*bucket) (*bucket)->prev = n;
    *bucket = n;
}

static inline void hnode_remove(hnode_t **bucket, hnode_t *n) {
    if (n->prev) n->prev->next = n->next;
    else *bucket = n->next;
    if (n->next) n->next->prev = n->prev;
}

/* recover the containing struct from an embedded hnode_t* */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
