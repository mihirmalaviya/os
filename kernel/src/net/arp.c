#include "net/arp.h"
#include "net/eth.h"
#include "net/block.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "sched/task.h"
#include <stdbool.h>
#include <stdint.h>

#define ARP_TABLE_SIZE 16

typedef enum {
   EMPTY,
   WAITING,
   KNOWN,
} arp_slot_state_t;

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    arp_slot_state_t state;
    net_device_t *dev; // where to send once resolved
    block_t *pending_head, *pending_tail;
} arp_entry_t;

static arp_entry_t arp_table[ARP_TABLE_SIZE];

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define HTYPE_ETHERNET 1
#define PTYPE_IPV4 0x0800

#define OP_REQUEST 1
#define OP_REPLY 2

// TODO this currently leaks if the packet is dropped, need a timer or sm
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
static void arp_send_reply(net_device_t *dev, const uint8_t asker_mac[6], uint32_t asker_ip) {
    block_t *b = block_alloc(sizeof(arp_header_t), HDR_LINK + sizeof(arp_header_t));
    if (b==NULL) return;
    arp_header_t *reply = (arp_header_t *)block_push(b, sizeof(arp_header_t));

    reply->htype = htons(HTYPE_ETHERNET);
    reply->ptype = htons(PTYPE_IPV4);
    reply->hlen = 6;
    reply->plen = 4;
    reply->opcode = htons(OP_REPLY);

    memcpy(reply->sender_mac, dev->mac, 6);
    reply->sender_ip = htonl(dev->ip);

    memcpy(reply->target_mac, asker_mac, 6);
    reply->target_ip = htonl(asker_ip);

    eth_send(dev, asker_mac, ETHERTYPE_ARP, b);
}

// broadcasts a question: "who has target_ip?"
static void arp_send_request(net_device_t *dev, uint32_t target_ip) {
    block_t *b = block_alloc(sizeof(arp_header_t), HDR_LINK + sizeof(arp_header_t));
    if (b==NULL) return;
    arp_header_t *request = (arp_header_t *)block_push(b, sizeof(arp_header_t));

    request->htype = htons(HTYPE_ETHERNET);
    request->ptype = htons(PTYPE_IPV4);
    request->hlen = 6;
    request->plen = 4;
    request->opcode = htons(OP_REQUEST);

    memcpy(request->sender_mac, dev->mac, 6);
    request->sender_ip = htonl(dev->ip);

    memset(request->target_mac, 0x00, 6); // 0 cus we dont know
    request->target_ip = htonl(target_ip);

    eth_send(dev, broadcast_mac, ETHERTYPE_ARP, b);
}

// insert or update an ip->mac
static void arp_learn(uint32_t ip, const uint8_t mac[6]) {
    lock_stuff();

    // update an existing entry for this ip
    for (int i=0; i<ARP_TABLE_SIZE; i++) {
        if (arp_table[i].state != EMPTY && arp_table[i].ip == ip) {
            bool was_waiting = arp_table[i].state==WAITING;
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].state = KNOWN;

            // detach the pending list under the lock, then send it unlocked
            block_t *pending = NULL;
            net_device_t *dev = NULL;
            if (was_waiting) {
                pending = arp_table[i].pending_head;
                dev = arp_table[i].dev;
                arp_table[i].pending_head = NULL;
                arp_table[i].pending_tail = NULL;
            }
            unlock_stuff();

            while (pending!=NULL) {
                block_t *next = pending->next;
                eth_send(dev, mac, ETHERTYPE_IP, pending);
                pending = next;
            }
            return;
        }
    }
    // else drop it into a free slot
    for (int i=0; i<ARP_TABLE_SIZE; i++) {
        if (arp_table[i].state == EMPTY) {
            arp_table[i].ip = ip;
            memcpy(arp_table[i].mac, mac, 6);
            arp_table[i].state = KNOWN;
            arp_table[i].pending_head = NULL;
            arp_table[i].pending_tail = NULL;
            unlock_stuff();
            return;
        }
    }
    // TODO some sort of eviction
    unlock_stuff();
}

// recieves requests and answers
void arp_process(net_device_t *dev, const void *buffer, size_t len) {
    if (len < sizeof(arp_header_t)) return;
    const arp_header_t *header = (const arp_header_t *)buffer;

    uint16_t opcode = ntohs(header->opcode);
    uint32_t target_ip = ntohl(header->target_ip);

    // note down their ip->mac
    arp_learn(ntohl(header->sender_ip), header->sender_mac);

    if (opcode==OP_REQUEST && target_ip==dev->ip) {
        arp_send_reply(dev, header->sender_mac, ntohl(header->sender_ip));
    }
}

void arp_resolve(net_device_t *dev, uint32_t ip, block_t *b) {
    lock_stuff();

    // already known: grab the mac and send outside the lock
    for (int i=0; i<ARP_TABLE_SIZE; i++) {
        if (arp_table[i].state==KNOWN && arp_table[i].ip==ip) {
            uint8_t mac[6];
            memcpy(mac, arp_table[i].mac, 6);
            unlock_stuff();
            eth_send(dev, mac, ETHERTYPE_IP, b);
            return;
        }
    }

    // already waiting on this ip, or make a new entry to wait in
    int slot=-1;
    for (int i=0; i<ARP_TABLE_SIZE; i++) {
        if (arp_table[i].state==WAITING && arp_table[i].ip==ip) {
            slot=i;
            break;
        }
    }

    bool need_request = false;
    if (slot==-1) {
        // not waiting yet either: find a free slot and kick off the request
        for (int i=0; i<ARP_TABLE_SIZE; i++) {
            if (arp_table[i].state==EMPTY) {
                slot=i;
                break;
            }
        }
        if (slot==-1) { // table full; TODO dynamically sizing hashmap or something of that sort
            unlock_stuff();
            block_free(b);
            return;
        }

        arp_table[slot].ip = ip;
        arp_table[slot].state = WAITING;
        arp_table[slot].dev = dev;
        arp_table[slot].pending_head = NULL;
        arp_table[slot].pending_tail = NULL;
        need_request = true;
    }

    // park it
    b->next = NULL;
    if (arp_table[slot].pending_tail!=NULL)
        arp_table[slot].pending_tail->next = b;
    else
        arp_table[slot].pending_head = b;
    arp_table[slot].pending_tail = b;

    unlock_stuff();

    // send the request outside the lock, same as the fast path's eth_send
    if (need_request)
        arp_send_request(dev, ip);
}
