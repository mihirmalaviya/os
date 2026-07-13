#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"

void ipv4_rx(netdev_t *dev, const void *frame, size_t len);
int ipv4_send(netdev_t *dev, uint32_t dst_ip, uint8_t protocol, const void *payload, size_t len);
