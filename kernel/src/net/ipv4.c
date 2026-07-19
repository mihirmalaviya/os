#include "net/ipv4.h"
#include "net/eth.h"
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

void ipv4_process(net_device_t *dev, const void *buffer, size_t len) {
    if (len < sizeof(ipv4_header_t)) return;
    const ipv4_header_t *header = (const ipv4_header_t *)buffer;

    if (checksum(header, sizeof(ipv4_header_t)) != 0)
        return; // corrupted header

    if (ntohs(header->flags_frag) & 0x3FFF) // TODO
        return; // fragmented

    if (ntohl(header->dst_ip) != dev->ip)
        return; // not addressed to us

    const void *payload = (const uint8_t *)buffer + sizeof(ipv4_header_t);
    size_t payload_len = len - sizeof(ipv4_header_t);

    switch (header->protocol) {
        case IPV4_PROTO_UDP:
            udp_process(payload, payload_len, ntohl(header->src_ip));
            break;
        case IPV4_PROTO_TCP:
            tcp_process(dev, payload, payload_len, ntohl(header->src_ip));
            break;
        case IPV4_PROTO_ICMP:
            // TODO: icmp_process(dev, payload, payload_len, ntohl(header->src_ip));
            break;
        default:
            kprintf("ipv4_process: unknown protocol %x, dropped\n", header->protocol);
            break;
    }
}

int ipv4_send(net_device_t *dev, uint32_t dst_ip, uint8_t protocol, pbuf_t *p) {
    size_t payload_len = pbuf_len(p); // before we prepend, this is just the payload

    // push our 20 bytes into the reserved headroom, in front of the payload
    ipv4_header_t *header = (ipv4_header_t *)pbuf_add_header(p, sizeof(ipv4_header_t));

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

    uint8_t dst_mac[6];
    if (arp_ask(dev, next_hop, dst_mac) != 0) {
        return -1; // couldnt resolve the next hops mac, give up
    }

    eth_send(dev, dst_mac, ETHERTYPE_IP, p);
    return 0;
}
