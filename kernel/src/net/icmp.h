#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"

void icmp_rx(netdev_t *dev, const void *frame, size_t len, uint32_t src_ip);

// sends an echo request and blocks until the matching reply arrives (or gives up).
// returns round-trip time in ms, or -1 on timeout.
int icmp_ping(netdev_t *dev, uint32_t dst_ip);
