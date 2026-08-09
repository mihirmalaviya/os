#include "net/epoll.h"
#include "kernel.h"
#include "sched/task.h"


void epoll_create(epoll_t *ep) {
    ep->ready_head=ep->ready_tail=NULL;
    ep->waiters=(waitq_t){0};
    semaphore_init(&ep->lock, 1);
}

void epoll_notify(tcpcb_t *tcb) {
    ASSERT(tcb->lock.current_count>0, "epoll_notify called without tcb lock held");

    epoll_t *ep = tcb->ep;
    if (ep==NULL)
        return;

    acquire_mutex(&ep->lock);
    if (!tcb->ep_ready){
        tcb->ep_ready = true;
        tcb->ep_next = NULL;
        tcb->ep_prev = ep->ready_tail;
        if (ep->ready_tail)
            ep->ready_tail->ep_next = tcb;
        else
            ep->ready_head = tcb;
        ep->ready_tail = tcb;
        waitq_broadcast(&ep->waiters);
    }
    release_mutex(&ep->lock);
}

void epoll_add(epoll_t *ep, tcpcb_t *tcb) {
    acquire_mutex(&tcb->lock);
    tcb->ep = ep;
    refcount_inc(&tcb->refcount); // epoll holds a ref for as long as its registered -
                                   // otherwise a close racing in the background (tcp_close
                                   // doesnt block) can recycle this tcb before epoll_remove runs

    epoll_notify(tcb);
    tcb_unlock(tcb);
}

void epoll_remove(epoll_t *ep, tcpcb_t *tcb) {
    acquire_mutex(&tcb->lock);
    tcb->ep=NULL;
    tcb_ref_dec(tcb); // release epoll's ref taken in epoll_add - may complete the recycle gate

    acquire_mutex(&ep->lock);
    if (tcb->ep_ready){
        if (tcb->ep_prev)
            tcb->ep_prev->ep_next = tcb->ep_next;
        else
            ep->ready_head = tcb->ep_next;

        if (tcb->ep_next)
            tcb->ep_next->ep_prev = tcb->ep_prev;
        else
            ep->ready_tail = tcb->ep_prev;

        tcb->ep_ready=false;
        tcb->ep_next=NULL;
        tcb->ep_prev=NULL;
    }
    release_mutex(&ep->lock);

    tcb_unlock(tcb);
}

tcpcb_t *epoll_wait(epoll_t *ep) {
    acquire_mutex(&ep->lock);
    
    // block till its ready
    while (ep->ready_head==NULL)
        waitq_wait(&ep->waiters, &ep->lock);

    // pop off the head
    tcpcb_t *tcb = ep->ready_head;
    ep->ready_head = tcb->ep_next;

    if (ep->ready_head==NULL)
        ep->ready_tail=NULL; // empty
    else
        ep->ready_head->ep_prev=NULL;

    tcb->ep_next=NULL;
    tcb->ep_ready=false;
    release_mutex(&ep->lock);

    return tcb;
}
