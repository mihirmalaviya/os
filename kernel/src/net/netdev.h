#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/block.h"

typedef struct net_device {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    int (*send)(struct net_device *dev, block_t *b);
} net_device_t;
