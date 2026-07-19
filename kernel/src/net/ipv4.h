#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"
#include "net/pkt.h"

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_TCP  6
#define IPV4_PROTO_UDP  17

void ipv4_process(net_device_t *dev, const void *buffer, size_t len);

int ipv4_send(net_device_t *dev, uint32_t dst_ip, uint8_t protocol, pbuf_t *p);
