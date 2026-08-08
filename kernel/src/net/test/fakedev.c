#include "net/test/fakedev.h"
#include "net/eth.h"
#include "lib/string.h"
#include "lib/hashmap.h" // container_of

static int fakedev_send(net_device_t *dev, block_t *b) {
    fakedev_t *fd = container_of(dev, fakedev_t, dev);

    fd->send_count++;
    if (fd->drop_every!=0 && fd->send_count % fd->drop_every==0) {
        block_free(b); // simulated loss
        return 0;
    }

    if (fd->outq_len>=FAKEDEV_OUTQ_SIZE) {
        block_free(b); // queue full, drop
        return 0;
    }

    fd->outq[fd->outq_tail] = b;
    fd->outq_tail = (fd->outq_tail+1)%FAKEDEV_OUTQ_SIZE;
    fd->outq_len++;
    return 0;
}

void fakedev_init(fakedev_t *fd, uint32_t ip, uint32_t netmask, const uint8_t mac[6]) {
    *fd = (fakedev_t){0};
    memcpy(fd->dev.mac, mac, 6);
    fd->dev.ip = ip;
    fd->dev.netmask = netmask;
    fd->dev.gateway = 0; //unused
    fd->dev.send = fakedev_send;
}

void fakedev_drain(fakedev_t *fd, net_device_t *peer) {
    while (fd->outq_len>0) {
        block_t *b = fd->outq[fd->outq_head];
        fd->outq_head = (fd->outq_head+1) % FAKEDEV_OUTQ_SIZE;
        fd->outq_len--;

        eth_process(peer, b);
    }
}
