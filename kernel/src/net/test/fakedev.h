#pragma once
#include <stdint.h>
#include "net/netdev.h"
#include "net/block.h"

#define FAKEDEV_OUTQ_SIZE 64

typedef struct fakedev {
    net_device_t dev;

    block_t *outq[FAKEDEV_OUTQ_SIZE];
    uint32_t outq_head, outq_tail, outq_len;

    uint32_t drop_every; // drops every nth frame
    uint32_t send_count;
} fakedev_t;

void fakedev_init(fakedev_t *fd, uint32_t ip, uint32_t netmask, const uint8_t mac[6]);

void fakedev_drain(fakedev_t *fd, net_device_t *peer);

static inline net_device_t *fakedev_netdev(fakedev_t *fd){
    return &fd->dev;
}
