#pragma once
#include "net/tcp.h"
#include "sched/task.h"

typedef struct epoll {
    tcpcb_t *ready_head, *ready_tail;
    waitq_t waiters;
    SEMAPHORE lock;
} epoll_t;

void epoll_create(epoll_t *ep);
void epoll_add(epoll_t *ep, tcpcb_t *tcb);
void epoll_remove(epoll_t *ep, tcpcb_t *tcb); // deregisters tcb
tcpcb_t *epoll_wait(epoll_t *ep); // blocks until something is ready and then pops if off
