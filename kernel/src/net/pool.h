#pragma once

#define POOL_NUM   32
#define POOL_SIZE  1600

void  pool_init(void);
void *pool_alloc(void);
void  pool_free(void *p);
