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

void eth_send(net_device_t *dev, const uint8_t dst_mac[6], uint16_t type, pbuf_t *p) {
    // push our 14 bytes into the reserved headroom, in front of the payload
    eth_header_t *header = (eth_header_t *)pbuf_add_header(p, sizeof(eth_header_t));

    memcpy(header->dst, dst_mac, 6);
    memcpy(header->src, dev->mac, 6);
    header->type = htons(type);

    dev->send(dev, p->data, pbuf_len(p));
}

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void eth_process(net_device_t *dev, const void *buffer, size_t len) {
    if (len<sizeof(eth_header_t)) return; // too short to hold our header
    const eth_header_t *header = (const eth_header_t *)buffer;
    
    if (memcmp(header->dst,dev->mac,6) != 0 && memcmp(header->dst,broadcast_mac,6) != 0)
        return;

    uint16_t type = ntohs(header->type);

    const void *payload = header+1;
    size_t payload_len = len-sizeof(eth_header_t);

    switch (type) {
        case ETHERTYPE_IP:
            ipv4_process(dev, payload, payload_len);
            break;
        case ETHERTYPE_ARP:
            arp_process(dev, payload, payload_len);
            break;
        default:
            kprintf("eth_process: unknown type %x, dropped\n", type);
            break;
    }
}
