#include "net/ipv4.h"
#include "net/eth.h"
#include "net/arp.h"
#include "net/udp.h"
#include "net/icmp.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "terminal/terminal.h"
#include <stdint.h>

#define IPV4_PROTO_ICMP 1
#define IPV4_PROTO_UDP  17

// the 20-byte header every ip packet starts with (no options)
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

static uint16_t ipv4_checksum(const void *data, size_t len) {
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;

    // add all the words into sum (word = 2 bytes)
    while (len > 1) {
        sum += *words++;
        len -= 2;
    }
    if (len == 1) { // odd byte left over
        sum += *(const uint8_t *)words;
    }

    // fold any carry back into the low 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

void ipv4_rx(netdev_t *dev, const void *frame, size_t len) {
    const ipv4_header_t *header = (const ipv4_header_t *)frame;

    if (ipv4_checksum(header, sizeof(ipv4_header_t)) != 0) 
        return; // corrupted header

    if (ntohl(header->dst_ip) != dev->ip)
        return; // not addressed to us

    const void *payload = (const uint8_t *)frame + sizeof(ipv4_header_t);
    size_t payload_len = len - sizeof(ipv4_header_t);

    switch (header->protocol) { // single byte, no ntohs needed
        case IPV4_PROTO_UDP:
            udp_rx(dev, payload, payload_len, ntohl(header->src_ip));
            break;
        case IPV4_PROTO_ICMP:
            icmp_rx(dev, payload, payload_len, ntohl(header->src_ip));
            break;
        default:
            kprintf("ipv4_rx: unknown protocol %x, dropped\n", header->protocol);
            break;
    }
}

int ipv4_send(netdev_t *dev, uint32_t dst_ip, uint8_t protocol, const void *payload, size_t len) {
    const size_t pkt_len = 14 + sizeof(ipv4_header_t) + len; // eth header + ip header + data
    uint8_t buf[pkt_len];
    ipv4_header_t *header = (ipv4_header_t *)(buf + 14);

    header->version_ihl = 0x45;   // version 4, header length 5 words
    header->service_type = 0;     // unused
    header->id = 0;               // only meaningful for fragmentation, so 0
    header->flags_frag = htons(0x4000); // "don't fragment" bit set
    header->ttl = 64;             // standard default hop limit

    header->total_length = htons(sizeof(ipv4_header_t) + len); // header + payload
    header->protocol     = protocol;
    header->src_ip       = htonl(dev->ip);
    header->dst_ip       = htonl(dst_ip);

    header->checksum = 0; // must be zero while computing, or youd be summing the old value in
    header->checksum = ipv4_checksum(header, sizeof(ipv4_header_t));

    uint32_t our_subnet = dev->ip & dev->netmask;
    uint32_t dst_subnet = dst_ip & dev->netmask;
    uint32_t next_hop;
    if (dst_subnet == our_subnet) {
        next_hop = dst_ip; // straight to destinaton
    } else {
        next_hop = dev->gateway; // let the router do it for us
    }

    uint8_t dst_mac[6];
    if (arp_ask(dev, next_hop, dst_mac) != 0) {
        return -1; // couldnt resolve the next hops mac, give up
    }

    memcpy(header + 1, payload, len); // payload goes right after the 20-byte header
    eth_send(dev, dst_mac, ETHERTYPE_IP, header, sizeof(ipv4_header_t) + len);
    return 0;
}
