#include "net/eth.h"
#include "net/byteorder.h"
#include "net/arp.h"
#include "net/ipv4.h"
#include "terminal/terminal.h"
#include "lib/string.h"
#include <stdint.h>

// the 14-byte header every frame starts with
typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type; // big-endian on the wire: 0x0800 = ip, 0x0806 = arp
} __attribute__((packed)) eth_header_t;

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void eth_rx(netdev_t *dev, const void *frame, size_t len) {
    if (len < 14) return; // the header is 14 long

    const eth_header_t *header = (const eth_header_t *)frame;

    // if destination!=us and destination!=broadcast then its not for us
    if (memcmp(header->dst, dev->mac, 6) != 0 && memcmp(header->dst, broadcast_mac, 6) != 0) 
        return;

    uint16_t type = ntohs(header->type); // big-endian -> little-endian

    const void *payload = (const uint8_t *)frame+14; // + 14 because header is 14 bytes
    size_t payload_len = len-14;

    switch (type) {
        case ETHERTYPE_IP:
            // kprintf("eth_rx: ip frame, %d bytes\n", (int)payload_len);
            ipv4_rx(dev, payload, payload_len);
            break;
        case ETHERTYPE_ARP:
            // kprintf("eth_rx: arp frame, %d bytes\n", (int)payload_len);
            arp_rx(dev, payload, payload_len);
            break;
        default:
            kprintf("eth_rx: unknown type %x, dropped\n", type);
            break;
    }
}

void eth_send(netdev_t *dev, const uint8_t dst_mac[6], uint16_t type, const void *payload, size_t len) {
    // caller must leave 14 bytes of headroom in front of payload -- step back into it
    eth_header_t *header = (eth_header_t *)((const uint8_t *)payload - 14);

    memcpy(header->dst, dst_mac, 6);
    memcpy(header->src, dev->mac, 6);
    header->type = htons(type);

    dev->send(dev, header, len+14); // header + payload, now one contiguous block
}
