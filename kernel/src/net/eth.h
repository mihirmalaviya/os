#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"
#include "net/pkt.h"

#define ETHERTYPE_IP  0x0800
#define ETHERTYPE_ARP 0x0806

typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type; // IP vs ARP
} __attribute__((packed)) eth_header_t;

void eth_process(net_device_t *dev, const void *buffer, size_t len);

void eth_send(net_device_t *dev, const uint8_t dst_mac[6], uint16_t type, pbuf_t *p);
