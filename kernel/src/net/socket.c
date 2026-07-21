#include "net/socket.h"
#include "net/tcp.h"
#include "fs/vfs.h"
#include "drivers/rtl8139.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_SOCKS 16

typedef enum {
    SOCK_NEW,
    SOCK_LISTEN,
    SOCK_CONN
} sock_kind_t;

typedef struct {
    bool in_use;
    sock_kind_t kind;
    uint16_t port;
    tcp_connection_t *conn;
} socket_t;

static socket_t sockets[MAX_SOCKS];

static int64_t sock_read(int id, void *buf, uint32_t n) {
    return tcp_recv(sockets[id].conn, buf, n);
}

static int64_t sock_write(int id, const void *buf, uint32_t n) {
    return tcp_send(sockets[id].conn, buf, n);
}

static int sock_close(int id) {
    if (sockets[id].conn) 
        tcp_close(sockets[id].conn);
    sockets[id].in_use=false;
    return 0;
}

static fs_operations_t socket_ops = {
    .read = sock_read,
    .write = sock_write,
    .close = sock_close,
};
static mountpoint_t socket_mp = { .operations = &socket_ops };

// find a free slot
static int sock_alloc(void) {
    for (int i=0; i<MAX_SOCKS; i++) {
        if (!sockets[i].in_use) {
            sockets[i] = (socket_t){0};
            sockets[i].in_use = true;
            sockets[i].kind = SOCK_NEW;
            return i;
        }
    }
    return -1; // table full
}

// hand out an unused local port for active opens
static uint16_t next_ephemeral(void) {
    static uint16_t port = 49152;
    return port++;
}

// fd -> sock
static socket_t *get_sock(int fd) {
    int id = vfs_fs_id(fd);
    if (id < 0 || id >= MAX_SOCKS || !sockets[id].in_use) return NULL;
    return &sockets[id];
}

// make a new socket, return the fd it registered to
int socket(void) {
    int id=sock_alloc();
    if (id<0) return -1;

    int fd = vfs_install_fd(&socket_mp, id);
    if (fd<0) {
        sockets[id].in_use = false;
        return -1;
    }
    return fd;
}

// assign it a port
int bind(int fd, uint16_t port) {
    socket_t *s = get_sock(fd);
    if (!s) return -1;
    s->port = port;
    return 0;
}

// listen for incoming handshakes
int listen(int fd) {
    socket_t *s = get_sock(fd);
    if (!s) return -1;
    if (tcp_listen(rtl8139_netdev(), s->port)<0)
        return -1;
    s->kind = SOCK_LISTEN;
    return 0;
}

// accept the first incamed handshake
int accept(int fd) {
    socket_t *s = get_sock(fd);
    if (!s) return -1;

    tcp_connection_t *c = tcp_accept(s->port); // blocks until a connection
    if (!c) return -1;

    int nid = sock_alloc();
    if (nid<0){
        tcp_close(c);
        return -1;
    }
    sockets[nid].kind=SOCK_CONN;
    sockets[nid].conn=c;

    int nfd = vfs_install_fd(&socket_mp, nid);
    if (nfd<0){
        tcp_close(c);
        sockets[nid].in_use=false;
        return -1;
    }
    return nfd;
}

int connect(int fd, uint32_t ip, uint16_t port) {
    socket_t *s = get_sock(fd);
    if (!s) return -1;

    tcp_connection_t *c = tcp_connect(rtl8139_netdev(), next_ephemeral(), ip, port); // blocks
    if (!c) return -1;

    s->kind=SOCK_CONN;
    s->conn=c;
    return 0;
}
