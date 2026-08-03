#pragma once
#include <stddef.h>
#include <stdint.h>
#include "net/pool.h"
#include "net/netdev.h"
#include "net/queue.h"
#include "lib/hashmap.h"

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *segment, size_t len);

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;  // high 4 bits = header length in 32-bit words; low 4 reserved
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;     // over pseudo-header + tcp header + payload
    uint16_t urgent_ptr;   // offset to end of urgent data
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

typedef struct {
    pool_node_t node;
    tcp_state_t t_state;

    hnode_t hnode;
    uint32_t local_ip, remote_ip;
    uint16_t local_port, remote_port;

    net_device_t *dev;
    uint32_t iss;
    uint32_t snd_una, snd_nxt, snd_max, snd_wnd;
    uint32_t rcv_nxt, irs;
    uint16_t t_mss;
    int flgcnt;
    queue_t sndq;
} tcpcb_t;

void tcp_init(void);
tcpcb_t *tcb_alloc(net_device_t *dev, uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);
void tcb_free(tcpcb_t *tcb);

void tcb_insert(tcpcb_t *tcb);
void tcb_remove(tcpcb_t *tcb);
tcpcb_t *tcb_lookup(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port);

typedef struct {
    pool_node_t node;
    hnode_t hnode;
    uint16_t port;
} listener_t;

listener_t *listener_create(uint16_t port); // NULL if already listening or pool empty
void listener_free(listener_t *l);

listener_t *listener_lookup(uint16_t port);

int tcp_listen(uint16_t port); // 0 on success, -1 if already listening or pool empty

uint16_t port_alloc(uint32_t local_ip, uint32_t remote_ip, uint16_t remote_port); // 0 on failure

void tcp_output(tcpcb_t *tcb);

void tcp_input(net_device_t *dev, const void *data, size_t len, uint32_t src_ip);
