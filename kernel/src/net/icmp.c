#include "net/icmp.h"
#include "net/ipv4.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "sched/task.h"
#include "arch/pit.h"
#include <stdint.h>

#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_ECHO_REPLY   0

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

typedef enum {
    ICMP_SLOT_EMPTY,
    ICMP_SLOT_WAITING,
    ICMP_SLOT_KNOWN,
} icmp_slot_state_t;

#define ICMP_TABLE_SIZE 8

typedef struct {
    uint16_t id;
    uint16_t sequence;
    icmp_slot_state_t state;
    uint64_t send_time;  // when the request went out
    uint64_t reply_time; // when the reply arrived
} icmp_entry_t;

static icmp_entry_t icmp_table[ICMP_TABLE_SIZE];

static uint16_t icmp_checksum(const void *data, size_t len) {
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *words++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)words;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

// builds and sends one echo request; does not wait for the reply
static int icmp_send(netdev_t *dev, uint32_t dst_ip, uint16_t id, uint16_t sequence) {
    icmp_header_t header;
    header.type = ICMP_TYPE_ECHO_REQUEST;
    header.code = 0;
    header.checksum = 0;
    header.id = htons(id);
    header.sequence = htons(sequence);
    header.checksum = icmp_checksum(&header, sizeof(header));

    return ipv4_send(dev, dst_ip, 1, &header, sizeof(header)); // 1 = icmp protocol number
}

void icmp_rx(netdev_t *dev, const void *frame, size_t len, uint32_t src_ip) {
    (void)dev;
    (void)src_ip;
    if (len < sizeof(icmp_header_t)) return;

    const icmp_header_t *header = (const icmp_header_t *)frame;
    if (header->type != ICMP_TYPE_ECHO_REPLY) return; // we only ever originate requests, not answer them (yet)

    uint16_t id = ntohs(header->id);
    uint16_t sequence = ntohs(header->sequence);

    for (int i=0; i<ICMP_TABLE_SIZE; i++) {
        if (icmp_table[i].state == ICMP_SLOT_WAITING &&
            icmp_table[i].id == id && icmp_table[i].sequence == sequence) {
            icmp_table[i].reply_time = get_time_since_boot();
            icmp_table[i].state = ICMP_SLOT_KNOWN;
            return;
        }
    }
    // no outstanding ping matches this reply -- ignore it
}

#define ICMP_PING_MAX_TRIES 8
#define ICMP_PING_BASE_DELAY_MS 10

int icmp_ping(netdev_t *dev, uint32_t dst_ip) {
    static uint16_t next_id = 1;
    static uint16_t next_sequence = 1;

    int slot = -1;
    for (int i=0; i<ICMP_TABLE_SIZE; i++) {
        if (icmp_table[i].state == ICMP_SLOT_EMPTY) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -1; // table full

    uint16_t id = next_id++;
    uint16_t sequence = next_sequence++;

    icmp_table[slot].id = id;
    icmp_table[slot].sequence = sequence;
    icmp_table[slot].state = ICMP_SLOT_WAITING;
    icmp_table[slot].send_time = get_time_since_boot();

    icmp_send(dev, dst_ip, id, sequence);

    uint64_t delay_ms = ICMP_PING_BASE_DELAY_MS;
    for (int tries=0; tries<ICMP_PING_MAX_TRIES; tries++) {
        ms_sleep(delay_ms);
        if (icmp_table[slot].state == ICMP_SLOT_KNOWN) {
            uint64_t rtt_ns = icmp_table[slot].reply_time - icmp_table[slot].send_time;
            icmp_table[slot].state = ICMP_SLOT_EMPTY;
            return (int)(rtt_ns / 1000000); // ns -> ms
        }
        delay_ms *= 2;
    }

    // gave up
    icmp_table[slot].state = ICMP_SLOT_EMPTY;
    return -1;
}
