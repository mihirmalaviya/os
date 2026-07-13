#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"

// called by ipv4_rx when a udp packet arrives
void udp_rx(netdev_t *dev, const void *frame, size_t len, uint32_t src_ip);

int udp_send(netdev_t *dev, uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
             const void *payload, size_t len);

// claims a port so udp_recv can be called on it. returns 0, or -1 if the bind table is full.
int udp_bind(uint16_t port);

// blocks the calling task until a packet arrives for this port.
// copies up to buflen bytes of payload into buf, fills src_ip/src_port with who sent it.
// returns the number of bytes copied, or -1 if the port was never bound.
int udp_recv(uint16_t port, uint32_t *src_ip, uint16_t *src_port, void *buf, size_t buflen);
