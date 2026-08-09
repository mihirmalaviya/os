#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "net/pool.h"
#include "net/netdev.h"
#include "net/queue.h"
#include "net/timer.h"
#include "sched/task.h"
#include "lib/hashmap.h"

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *segment, size_t len);

#define TCP_EOF -1 // receive side ended cleanly
#define ECONNRESET 104 // peer sent RST
#define ETIMEDOUT 110 // retransmit retries exhausted w/o ACK

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset; // high 4 bits = header length in 32-bit words; low 4 reserved
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum; // over pseudo-header + tcp header + payload
    uint16_t urgent_ptr; // offset to end of urgent data
} __attribute__((packed)) tcp_header_t;

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10
#define URG 0x20
#define ECE 0x40
#define CWR 0x80

typedef enum {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT,
} tcp_state_t;

#define SEQ_LT(a,b)  ((int32_t)((a)-(b)) < 0)
#define SEQ_LEQ(a,b) ((int32_t)((a)-(b)) <= 0)
#define SEQ_GT(a,b)  ((int32_t)((a)-(b)) > 0)
#define SEQ_GEQ(a,b) ((int32_t)((a)-(b)) >= 0)
#define SEQ_MAX(a,b) (SEQ_GT((a),(b)) ? (a) : (b))
#define SEQ_MIN(a,b) (SEQ_LT((a),(b)) ? (a) : (b))

typedef struct listener listener_t;
typedef struct socket socket_t;
typedef struct epoll epoll_t;

typedef struct tcpcb {
    pool_node_t node;
    tcp_state_t state;

    hnode_t hnode;
    uint32_t local_ip, remote_ip;
    uint16_t local_port, remote_port;

    socket_t *sock;

    net_device_t *dev;
    uint32_t iss;
    uint32_t snd_una, snd_nxt, snd_max, snd_wnd;
    uint64_t bytes_sent; // total non retransmitted bytes sent
    uint64_t bytes_received; // total payload bytes delivered to rcvq
    uint32_t t_rtseq;    // snd_nxt of the segment currently being timed
    uint64_t rtt_start;  // 0 = not timing anything
    int32_t  srtt, rttvar;
    uint32_t snd_cwnd, snd_ssthresh;
    uint32_t rcv_nxt, irs;
    uint32_t rcv_adv; // rcv_nxt+window as of the last segment we actually sent (plan9's wsnt)
    uint16_t mss;
    int synfin_cnt;
    queue_t sndq;
    waitq_t writers; // woken when sndq has room (snd_una advances)

    queue_t rcvq;
    timer_t t_rxt_timer;
    uint8_t t_rxtcount;
    timer_t t_2msl_timer; // FIN_WAIT_2 and TIME_WAIT

    struct tcpcb *accept_next; // listeners accept queue

    waitq_t readers; // woken when data lands in rcvq
    waitq_t connecting; // woken when state leaves SYN_SENT

    int error; // 0=still open
    int refcount;

    epoll_t *ep;
    struct tcpcb *ep_next, *ep_prev; // intrusive ready-list links, owned by ep->lock
    bool ep_ready; // epoll ready list

    SEMAPHORE lock;
} tcpcb_t;

void tcp_init(void);
uint64_t tcp_retransmit_count(void); // global, bumped once per actual retransmit
tcpcb_t *tcb_alloc(net_device_t *dev, uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);
void tcb_free(tcpcb_t *tcb);
bool tcb_try_recycle(tcpcb_t *tcb); // caller must hold lock
void tcb_unlock(tcpcb_t *tcb); // caller must hold lock, unlocks tcb->lock
void tcb_close(tcpcb_t *tcb, int error); // caller must hold lock

void tcb_ref_dec(tcpcb_t *tcb); // caller must hold lock cus it tries to free

void tcb_insert(tcpcb_t *tcb);
void tcb_remove(tcpcb_t *tcb);
tcpcb_t *tcb_lookup(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);
tcpcb_t *tcb_lookup_locked(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port); // returns locked

struct listener {
    pool_node_t node;
    hnode_t hnode;
    uint16_t port;

    tcpcb_t *accept_head, *accept_tail;
    int nqueued;
    bool closing;
    int refcount;
    SEMAPHORE lock;
    waitq_t accept_waiters;
};

listener_t *listener_create(uint16_t port); // NULL if already listening or pool empty
void listener_close(listener_t *l);

listener_t *listener_lookup(uint16_t port);
listener_t *listener_lookup_locked(uint16_t port); // returns locked

void listener_ref_dec(listener_t *l); // caller must hold lock

int tcp_listen(uint16_t port); // 0 on success, -1 if already listening or pool empty
tcpcb_t *tcp_accept(listener_t *l); // blocks until a connection is ready

uint16_t port_alloc(uint32_t local_ip, uint32_t remote_ip, uint16_t remote_port); // 0 on failure

int64_t tcp_send(tcpcb_t *tcb, const void *data, size_t len); // bytes queued, -1 on failure
int64_t tcp_recv(tcpcb_t *tcb, void *buf, size_t n);
int64_t tcp_recv_nb(tcpcb_t *tcb, void *buf, size_t n); // -2 instead of blocking if rcvq is empty
void tcp_close(tcpcb_t *tcb); // calling tcp_send or recv or anything like that after this is undefined behavior

tcpcb_t *tcp_connect(net_device_t *dev, uint32_t dst_ip, uint16_t dst_port); // NULL on failure

void epoll_notify(tcpcb_t *tcb);


#define FORCE 0x1
// #define RETRAN 0x2
void tcp_output(tcpcb_t *tcb, int flags);

void tcp_input(net_device_t *dev, block_t *b, uint32_t src_ip);
