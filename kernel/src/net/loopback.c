#include "net/loopback.h"
#include "net/eth.h"

static netdev_t loopback_netdev;

// "sending" on loopback = the frame instantly arrives back on the same card
static int loopback_send(netdev_t *dev, const void *frame, size_t len) {
    eth_rx(dev, frame, len);
    return 0;
}

void loopback_init(void) {
    // mac stays 00:00:00:00:00:00 -- the frame never touches a wire,
    // so no switch ever needs a real address
    loopback_netdev.ip      = 0x7F000001; // 127.0.0.1
    loopback_netdev.netmask = 0xFF000000; // 255.0.0.0, the whole 127.x block
    loopback_netdev.gateway = 0;          // everything is local, never used
    loopback_netdev.send    = loopback_send;
}

netdev_t *loopback_netdev_get(void) {
    return &loopback_netdev;
}
