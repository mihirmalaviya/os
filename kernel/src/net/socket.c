#include "net/socket.h"
#include "net/tcp.h"
#include "drivers/e1000.h"
#include "fs/vfs.h"
#include "sched/task.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_SOCKS 15000

typedef enum {
    SOCK_NEW,
    SOCK_LISTEN,
    SOCK_CONN,
    SOCK_CLOSED
} sock_state_t;

typedef struct socket {
    bool in_use;
    sock_state_t state;
    uint16_t port;
    tcpcb_t *tcb;         // set once state==SOCK_CONN
    listener_t *listener; // set once state==SOCK_LISTEN

    SEMAPHORE lock; // protects socket's fields
} socket_t;

static socket_t sockets[MAX_SOCKS];

static int64_t sock_read(int id, void *buf, uint32_t n) {
    socket_t *s = &sockets[id];
    acquire_mutex(&s->lock);
    if (s->state!=SOCK_CONN){
        release_mutex(&s->lock);
        return -1;
    }
    tcpcb_t *tcb = s->tcb;
    release_mutex(&s->lock);

    return tcp_recv(tcb, buf, n);
}

static int64_t sock_write(int id, const void *buf, uint32_t n) {
    socket_t *s = &sockets[id];
    acquire_mutex(&s->lock);
    if (s->state!=SOCK_CONN){
        release_mutex(&s->lock);
        return -1;
    }
    tcpcb_t *tcb = s->tcb;
    release_mutex(&s->lock);

    int64_t written = tcp_send(tcb, buf, n);
    if (written<0)
        return -tcb->error;
    return written;
}

static int sock_close(int id) {
    socket_t *s = &sockets[id];
    acquire_mutex(&s->lock);
    if (s->state==SOCK_CLOSED){
        release_mutex(&s->lock);
        return -1;
    }
    sock_state_t state = s->state;

    tcpcb_t *tcb = s->tcb;
    listener_t *listener = s->listener;
    s->state = SOCK_CLOSED;
    release_mutex(&s->lock);

    switch (state) {
        case SOCK_CONN:
            if (tcb)
                tcp_close(tcb);
            break;
        case SOCK_LISTEN:
            if (listener)
                listener_close(listener);
            break;
        default:
            break;
    }

    s->in_use=false;
    return 0;
}

static fs_operations_t socket_ops = {
    .read = sock_read,
    .write = sock_write,
    .close = sock_close,
};
static mountpoint_t socket_mp = { .operations = &socket_ops };

static int sock_alloc(void) {
    lock_stuff();
    for (int i=0; i<MAX_SOCKS; i++) {
        if (!sockets[i].in_use) {
            sockets[i] = (socket_t){0};
            sockets[i].in_use = true;
            sockets[i].state = SOCK_NEW;
            semaphore_init(&sockets[i].lock, 1);
            unlock_stuff();
            return i;
        }
    }
    unlock_stuff();
    return -1;
}

static socket_t *get_sock(int fd) {
    int id = vfs_fs_id(fd);
    if (id < 0 || id >= MAX_SOCKS || !sockets[id].in_use) return NULL;
    return &sockets[id];
}

int socket(void) {
    int id = sock_alloc();
    if (id<0) return -1;

    int fd = vfs_install_fd(&socket_mp, id);
    if (fd<0) {
        sockets[id].in_use = false;
        return -1;
    }
    return fd;
}

int bind(int fd, uint16_t port) {
    socket_t *s = get_sock(fd);
    if (!s)
        return -1;
    s->port = port;
    return 0;
}

int listen(int fd) {
    socket_t *s = get_sock(fd);
    if (!s)
        return -1;

    acquire_mutex(&s->lock);
    if (s->state!=SOCK_NEW){
        release_mutex(&s->lock);
        return -1;
    }
    release_mutex(&s->lock);

    listener_t *l = listener_create(s->port);
    if (l==NULL)
        return -1;

    acquire_mutex(&s->lock);
    if (s->state!=SOCK_NEW){
        // close() got called since we released the lock
        release_mutex(&s->lock);
        listener_close(l);
        return -1;
    }
    s->listener = l;
    s->state = SOCK_LISTEN;
    release_mutex(&s->lock);

    return 0;
}

int accept(int fd) {
    socket_t *s = get_sock(fd);
    if (!s)
        return -1;

    acquire_mutex(&s->lock);
    if (s->state!=SOCK_LISTEN){
        release_mutex(&s->lock);
        return -1;
    }

    listener_t *listener = s->listener;
    release_mutex(&s->lock);

    tcpcb_t *tcb = tcp_accept(listener);
    if (tcb==NULL)
        return -1; 

    int nid = sock_alloc();
    if (nid<0){
        tcp_close(tcb);
        return -1;
    }
    sockets[nid].state = SOCK_CONN;
    sockets[nid].tcb = tcb;

    acquire_mutex(&tcb->lock);
    tcb->sock = &sockets[nid];
    tcb_unlock(tcb);

    int nfd = vfs_install_fd(&socket_mp, nid);
    if (nfd<0){
        tcp_close(tcb);
        sockets[nid].in_use=false;
        return -1;
    }
    return nfd;
}

tcpcb_t *sock_tcb(int fd) {
    socket_t *s = get_sock(fd);
    if (!s) return NULL;

    acquire_mutex(&s->lock);
    tcpcb_t *tcb = (s->state==SOCK_CONN) ? s->tcb : NULL;
    release_mutex(&s->lock);
    return tcb;
}

static int connect_common(int fd, uint32_t ip, uint16_t port, bool block) {
    socket_t *s = get_sock(fd);
    if (!s) return -1;

    acquire_mutex(&s->lock);
    if (s->state!=SOCK_NEW){
        release_mutex(&s->lock);
        return -1;
    }
    release_mutex(&s->lock);

    tcpcb_t *tcb = tcp_connect(e1000_netdev(), ip, port); // fires the SYN, returns in SYN_SENT
    if (tcb==NULL)
        return -1;

    acquire_mutex(&s->lock);
    if (s->state==SOCK_CLOSED){
        // close() got called since we released the lock
        release_mutex(&s->lock);
        tcp_close(tcb);
        return -1;
    }
    s->state = SOCK_CONN;
    s->tcb = tcb;
    release_mutex(&s->lock);

    acquire_mutex(&tcb->lock);
    tcb->sock = s;

    if (!block) { // caller just wanted SYN fired
        tcb_unlock(tcb);
        return 0;
    }

    // block until the handshake actually finishes, one way or the other
    while (tcb->state==SYN_SENT && tcb->error==0)
        waitq_wait(&tcb->connecting, &tcb->lock);
    int err = tcb->error;
    tcb_unlock(tcb); // frees lock

    if (err!=0)
        return -err;

    return 0;
}

int connect(int fd, uint32_t ip, uint16_t port) {
    return connect_common(fd, ip, port, true);
}

int connect_nb(int fd, uint32_t ip, uint16_t port) {
    return connect_common(fd, ip, port, false);
}
