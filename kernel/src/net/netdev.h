#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct netdev {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    int (*send)(struct netdev *dev, const void *frame, size_t len);
} netdev_t;
