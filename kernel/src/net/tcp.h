#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"

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

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
} tcp_state_t;

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10
#define URG 0x20
#define ECE 0x40
#define CWR 0x80

// opaque connection handle; the full struct lives in tcp.c
typedef struct tcp_connection tcp_connection_t;

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *segment, size_t len);

// park a slot in LISTEN on the given port, ready to accept connections
int tcp_listen(net_device_t *dev, uint16_t port);

// block until a connection completes its handshake on port, return its handle
tcp_connection_t *tcp_accept(uint16_t port);

// block until data arrives, copy up to len bytes into buf, return count (0 = EOF)
int tcp_recv(tcp_connection_t *c, void *buf, size_t len);

// queue len bytes for sending, return bytes accepted (short write), or -1 if not sendable
int tcp_send(tcp_connection_t *c, const void *buf, size_t len);

// close our sending side (send FIN); call after the peer has closed (recv returned 0)
int tcp_close(tcp_connection_t *c);

// actively open a connection from src_port to dst; blocks until established, NULL on failure
tcp_connection_t *tcp_connect(net_device_t *dev, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port);

void tcp_process(net_device_t *dev, const void *buffer, size_t len, uint32_t src_ip);

// periodically retransmits stuff
// void tcp_timer_task(void);
