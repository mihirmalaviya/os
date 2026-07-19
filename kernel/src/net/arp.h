#pragma once
#include <stddef.h>
#include "net/netdev.h"

void arp_process(net_device_t *dev, const void *buffer, size_t len);

// blocking
int arp_ask(net_device_t *dev, uint32_t ip, uint8_t mac_out[6]);
