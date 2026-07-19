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
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
} tcp_state_t;

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10
#define URG 0x20
#define ECE 0x40
#define CWR 0x80

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *segment, size_t len);

// park a slot in LISTEN on the given port, ready to accept connections
int tcp_listen(net_device_t *dev, uint16_t port);

void tcp_process(net_device_t *dev, const void *buffer, size_t len, uint32_t src_ip);
