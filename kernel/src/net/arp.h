#pragma once
#include <stddef.h>
#include "net/netdev.h"

void arp_rx(netdev_t *dev, const void *frame, size_t len);

// resolves ip to a mac, asking the network if we don't already know it.
// blocks the calling task until an answer arrives. returns 0 on success, -1 if the table is full
int arp_ask(netdev_t *dev, uint32_t ip, uint8_t mac_out[6]);
