#pragma once
#include <stddef.h>
#include "net/netdev.h"
#include "net/block.h"

void arp_process(net_device_t *dev, const void *buffer, size_t len);

void arp_resolve(net_device_t *dev, uint32_t ip, block_t *b);
