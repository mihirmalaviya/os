#include "net/ipv4.h"
#include "net/arp.h"
#include "net/udp.h"
#include "net/tcp.h"
#include "net/checksum.h"
#include "net/byteorder.h"
#include "terminal/terminal.h"
#include <stdint.h>

typedef struct {
    uint8_t  version_ihl;   // version (4 bits) + header length in 32-bit words (4 bits)
    uint8_t  service_type;
    uint16_t total_length;  // header + payload
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;      // 1 = icmp, 17 = udp
    uint16_t checksum;      // header only
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ipv4_header_t;

void ipv4_process(net_device_t *dev, block_t *b) {
    if (block_len(b) < sizeof(ipv4_header_t)) { block_free(b); return; }
    const ipv4_header_t *header = (const ipv4_header_t *)b->data;

    if (checksum(header, sizeof(ipv4_header_t)) != 0) {
        block_free(b);
        return; // corrupted header
    }

    if (ntohs(header->flags_frag) & 0x3FFF) { // TODO
        block_free(b);
        return; // fragmented
    }

    if (ntohl(header->dst_ip) != dev->ip) {
        block_free(b);
        return; // not addressed to us
    }

    size_t total_len = ntohs(header->total_length);
    if (total_len < sizeof(ipv4_header_t) || total_len > block_len(b)) {
        block_free(b);
        return; // too small to fit header, or total_len too big
    }

    size_t payload_len = total_len - sizeof(ipv4_header_t);
    block_pull(b, sizeof(ipv4_header_t));
    block_trim(b, payload_len);

    switch (header->protocol) {
        case IPV4_PROTO_UDP:
            udp_process(b->data, block_len(b), ntohl(header->src_ip));
            block_free(b);
            break;
        case IPV4_PROTO_TCP:
            tcp_input(dev, b->data, block_len(b), ntohl(header->src_ip));
            block_free(b);
            break;
        case IPV4_PROTO_ICMP:
            // TODO: icmp_process(dev, payload, payload_len, ntohl(header->src_ip));
            block_free(b);
            break;
        default:
            kprintf("ipv4_process: unknown protocol %x, dropped\n", header->protocol);
            block_free(b);
            break;
    }
}

int ipv4_send(net_device_t *dev, uint32_t dst_ip, uint8_t protocol, block_t *b) {
    size_t payload_len = block_len(b); // before we prepend, this is just the payload

    // push our 20 bytes into the reserved headroom, in front of the payload
    ipv4_header_t *header = (ipv4_header_t *)block_push(b, sizeof(ipv4_header_t));

    header->version_ihl  = 0x45;          // version 4, header length 5 words
    header->service_type = 0;
    header->id           = 0;
    header->flags_frag   = htons(0x4000); // dont fragment
    header->ttl          = 64;

    header->total_length = htons(sizeof(ipv4_header_t) + payload_len);
    header->protocol     = protocol;
    header->src_ip       = htonl(dev->ip);
    header->dst_ip       = htonl(dst_ip);

    header->checksum = 0;
    header->checksum = checksum(header, sizeof(ipv4_header_t));

    uint32_t next_hop;
    if ((dst_ip & dev->netmask) == (dev->ip & dev->netmask)) { // in our lan?
        next_hop = dst_ip;
    } else {
        next_hop = dev->gateway; // no? send to router
    }

    arp_resolve(dev, next_hop, b); // sends now if known, else parks until resolved
    return 0;
}
