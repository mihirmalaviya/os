#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"

#define ETHERTYPE_IP  0x0800
#define ETHERTYPE_ARP 0x0806

// entry point for every arriving frame; drivers call this from their receive path
void eth_rx(netdev_t *dev, const void *frame, size_t len);

// caller must leave 14 bytes of headroom before payload so we can write our header
void eth_send(netdev_t *dev, const uint8_t dst_mac[6], uint16_t type, const void *payload, size_t len);
