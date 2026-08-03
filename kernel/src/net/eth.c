#include "net/eth.h"
#include "net/arp.h"
#include "net/ipv4.h"
#include "net/byteorder.h"
#include "terminal/terminal.h"
#include "lib/string.h"
#include <stdint.h>

// typedef struct {
//     uint8_t  dst[6];
//     uint8_t  src[6];
//     uint16_t type;
// } __attribute__((packed)) eth_header_t;

void eth_send(net_device_t *dev, const uint8_t dst_mac[6], uint16_t type, block_t *b) {
    // push our 14 bytes into the reserved headroom, in front of the payload
    eth_header_t *header = (eth_header_t *)block_push(b, sizeof(eth_header_t));

    memcpy(header->dst, dst_mac, 6);
    memcpy(header->src, dev->mac, 6);
    header->type = htons(type);

    if (block_len(b) < 60) {
        uint32_t pad = 60 - block_len(b);
        memset(block_put(b, pad), 0, pad);
    }

    dev->send(dev, b);
}

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void eth_process(net_device_t *dev, block_t *b) {
    if (block_len(b)<sizeof(eth_header_t)) { block_free(b); return; } // too short to hold our header
    const eth_header_t *header = (const eth_header_t *)b->data;

    if (memcmp(header->dst,dev->mac,6) != 0 && memcmp(header->dst,broadcast_mac,6) != 0) {
        block_free(b);
        return;
    }

    uint16_t type = ntohs(header->type);
    block_pull(b, sizeof(eth_header_t));

    switch (type) {
        case ETHERTYPE_IP:
            ipv4_process(dev, b);
            break;
        case ETHERTYPE_ARP:
            arp_process(dev, b->data, block_len(b));
            block_free(b);
            break;
        default:
            kprintf("eth_process: unknown type %x, dropped\n", type);
            block_free(b);
            break;
    }
}
