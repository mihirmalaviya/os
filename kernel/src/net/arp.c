#include "net/arp.h"
#include "net/eth.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "sched/task.h"
#include <stdint.h>

#define ARP_TABLE_SIZE 8

typedef enum {
    ARP_SLOT_EMPTY,
    ARP_SLOT_WAITING,
    ARP_SLOT_KNOWN,
} arp_slot_state_t;

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    arp_slot_state_t state;
} arp_entry_t;

#define ARP_ASK_MAX_TRIES 8
#define ARP_ASK_BASE_DELAY_MS 1 // 1+2+4+8+16+32+64+128 = 255ms worst case

static arp_entry_t arp_table[ARP_TABLE_SIZE];

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4     0x0800

// the 28-byte body carried inside an eth frame of type 0x0806
typedef struct {
    uint16_t htype;  // hardware type: 1 = ethernet
    uint16_t ptype;  // protocol type: 0x0800 = ip
    uint8_t  hlen;   // hardware addr length: 6 (mac)
    uint8_t  plen;   // protocol addr length: 4 (ip)
    uint16_t opcode; // 1 = request, 2 = reply
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed)) arp_header_t;

// answer someones question with our mac
static void arp_send_reply(netdev_t *dev, const uint8_t asker_mac[6], uint32_t asker_ip) {
    const size_t pkt_len = 14 + sizeof(arp_header_t); // eth header + arp body
    uint8_t buf[pkt_len];
    arp_header_t *reply = (arp_header_t *)(buf + 14);

    reply->htype = htons(ARP_HTYPE_ETHERNET);
    reply->ptype = htons(ARP_PTYPE_IPV4);
    reply->hlen = 6;
    reply->plen = 4;
    reply->opcode = htons(ARP_OP_REPLY);

    memcpy(reply->sender_mac, dev->mac, 6);
    reply->sender_ip = htonl(dev->ip);

    memcpy(reply->target_mac, asker_mac, 6);
    reply->target_ip = htonl(asker_ip);

    eth_send(dev, asker_mac, ETHERTYPE_ARP, reply, sizeof(arp_header_t));
}

// broadcasts a question: "who has target_ip?"
static void arp_send_request(netdev_t *dev, uint32_t target_ip) {
    const size_t pkt_len = 14 + sizeof(arp_header_t); // eth header + arp body
    uint8_t buf[pkt_len];
    arp_header_t *request = (arp_header_t *)(buf + 14);

    request->htype = htons(ARP_HTYPE_ETHERNET);
    request->ptype = htons(ARP_PTYPE_IPV4);
    request->hlen = 6;
    request->plen = 4;
    request->opcode = htons(ARP_OP_REQUEST);

    memcpy(request->sender_mac, dev->mac, 6);
    request->sender_ip = htonl(dev->ip);

    memset(request->target_mac, 0x00, 6); // 0 cus we dont know
    request->target_ip = htonl(target_ip);

    eth_send(dev, broadcast_mac, ETHERTYPE_ARP, request, sizeof(arp_header_t));
}

// recieves requests and answers
void arp_rx(netdev_t *dev, const void *frame, size_t len) {
    const arp_header_t *header = (const arp_header_t *)frame;

    uint16_t opcode = ntohs(header->opcode);
    uint32_t target_ip = ntohl(header->target_ip);

    if (opcode == ARP_OP_REQUEST && target_ip == dev->ip) {
        arp_send_reply(dev, header->sender_mac, ntohl(header->sender_ip));
    }

    if (opcode == ARP_OP_REPLY) {
        uint32_t sender_ip = ntohl(header->sender_ip);

        // find the slot waiting on this ip; if nobody asked, ignore it
        for (int i=0; i<ARP_TABLE_SIZE; i++) {
            if (arp_table[i].state == ARP_SLOT_WAITING && arp_table[i].ip == sender_ip) {
                memcpy(arp_table[i].mac, header->sender_mac, 6);
                arp_table[i].state = ARP_SLOT_KNOWN;
                break;
            }
        }
    }
}

int arp_ask(netdev_t *dev, uint32_t ip, uint8_t mac_out[6]) {
    // already known; no need to ask again
    for (int i=0; i<ARP_TABLE_SIZE; i++) {
        if (arp_table[i].state == ARP_SLOT_KNOWN && arp_table[i].ip == ip) {
            memcpy(mac_out, arp_table[i].mac, 6);
            return 0;
        }
    }

    // find a free slot to wait in
    int slot = -1;
    for (int i=0; i<ARP_TABLE_SIZE; i++) {
        if (arp_table[i].state == ARP_SLOT_EMPTY) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1; // table full; TODO dynamically sizing hashmap or something of that sort

    arp_table[slot].ip = ip;
    arp_table[slot].state = ARP_SLOT_WAITING;

    arp_send_request(dev, ip);

    // check immediately
    if (arp_table[slot].state == ARP_SLOT_KNOWN) {
        memcpy(mac_out, arp_table[slot].mac, 6);
        return 0;
    }
    // TODO, right now it polls with exponential backoff, eventually make this into an event listener with timeout
    uint64_t delay_ms = ARP_ASK_BASE_DELAY_MS;
    for (int tries=0; tries<ARP_ASK_MAX_TRIES; tries++) {
        ms_sleep(delay_ms);
        if (arp_table[slot].state == ARP_SLOT_KNOWN) {
            memcpy(mac_out, arp_table[slot].mac, 6);
            return 0;
        }
        delay_ms *= 2;
    }

    // give up and return error
    arp_table[slot].state = ARP_SLOT_EMPTY;
    return -1;
}
