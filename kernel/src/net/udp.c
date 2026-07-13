#include "net/udp.h"
#include "net/ipv4.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "terminal/terminal.h"
#include "sched/task.h"
#include <stdint.h>

#define UDP_PROTO 17

#define UDP_BIND_TABLE_SIZE 16
#define UDP_RING_SIZE 8192 // total bytes reserved per bound port, shared across however many packets fit

// each queued packet is stored back-to-back in the ring as: this header, then `len` bytes of data
typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
} udp_record_header_t;

typedef struct {
    uint16_t port;
    int in_use;

    uint8_t ring[UDP_RING_SIZE];
    size_t write_pos; // next byte udp_rx writes to
    size_t read_pos;  // next byte udp_recv reads from
    size_t used;      // bytes currently occupied, across all queued packets

    SEMAPHORE *sem; // signals "at least one full packet is available"
} udp_bind_entry_t;

static udp_bind_entry_t udp_bind_table[UDP_BIND_TABLE_SIZE];

// write to ring buffer
static void ring_write(udp_bind_entry_t *entry, const uint8_t *src, size_t n) {
    for (size_t i=0; i<n; i++) {
        entry->ring[entry->write_pos] = src[i];
        entry->write_pos = (entry->write_pos + 1) % UDP_RING_SIZE;
    }
}

// read from ring buffer
static void ring_read(udp_bind_entry_t *entry, uint8_t *dst, size_t n) {
    for (size_t i=0; i<n; i++) {
        uint8_t byte = entry->ring[entry->read_pos];
        if (dst) dst[i] = byte;
        entry->read_pos = (entry->read_pos + 1) % UDP_RING_SIZE;
    }
}

// the 8-byte header every udp packet starts with
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

void udp_rx(netdev_t *dev, const void *frame, size_t len, uint32_t src_ip) {
    const udp_header_t *header = (const udp_header_t *)frame;
    uint16_t dst_port = ntohs(header->dst_port);
    size_t data_len = len - sizeof(udp_header_t);

    // find the right port
    for (int i = 0; i < UDP_BIND_TABLE_SIZE; i++) {
        if (!udp_bind_table[i].in_use || udp_bind_table[i].port != dst_port)
            continue;

        udp_bind_entry_t *entry = &udp_bind_table[i];
        size_t record_size = sizeof(udp_record_header_t) + data_len;

        // if it cant fit drop it
        if (entry->used + record_size > UDP_RING_SIZE)
            return;

        // since we have parsed the udp packet already we just need to keep track of these 3 things
        udp_record_header_t rec;
        rec.src_ip = src_ip;
        rec.src_port = ntohs(header->src_port);
        rec.len = (uint16_t)data_len;

        // write the record header and the data we recieved
        ring_write(entry, (const uint8_t *)&rec, sizeof(rec));
        ring_write(entry, (const uint8_t *)frame + sizeof(udp_header_t), data_len);
        entry->used += record_size;

        release_semaphore(entry->sem);
        return;
    }
    // nothing matched :(
}

int udp_send(netdev_t *dev, uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
             const void *payload, size_t len) {
    const size_t pkt_len = sizeof(udp_header_t) + len;
    uint8_t buf[pkt_len];
    udp_header_t *header = (udp_header_t *)buf;

    header->src_port = htons(src_port);
    header->dst_port = htons(dst_port);
    header->length = htons((uint16_t)pkt_len);
    header->checksum = 0; // TODO checksum

    memcpy(buf + sizeof(udp_header_t), payload, len);
    return ipv4_send(dev, dst_ip, UDP_PROTO, buf, pkt_len);
}

// find a free space in the bind table and bind the port in our table; if there is no space return -1
// TODO dynamically sized dictionary or some other good data structure for this
int udp_bind(uint16_t port) {
    for (int i = 0; i < UDP_BIND_TABLE_SIZE; i++) {
        if (!udp_bind_table[i].in_use) {
            udp_bind_table[i].port = port;
            udp_bind_table[i].write_pos = 0;
            udp_bind_table[i].read_pos = 0;
            udp_bind_table[i].used = 0;

            udp_bind_table[i].sem = create_semaphore(1);
            acquire_semaphore(udp_bind_table[i].sem);

            udp_bind_table[i].in_use = 1;
            return 0;
        }
    }
    return -1; // table full
}

int udp_recv(uint16_t port, uint32_t *src_ip, uint16_t *src_port, void *buf, size_t buflen) {
    udp_bind_entry_t *entry = NULL;

    // find the port
    for (int i = 0; i < UDP_BIND_TABLE_SIZE; i++) {
        if (udp_bind_table[i].in_use && udp_bind_table[i].port == port) {
            entry = &udp_bind_table[i];
            break;
        }
    }
    if (!entry) return -1; // port was never bound

    acquire_semaphore(entry->sem); // blocks here until udp_rx enqueues something

    // read the header
    udp_record_header_t rec;
    ring_read(entry, (uint8_t *)&rec, sizeof(rec));

    // read the datas
    size_t copy_len = rec.len < buflen ? rec.len : buflen;
    ring_read(entry, (uint8_t *)buf, copy_len);

    // TODO signal truncation so user knows we truncated, maube return actual len instead of copylen, idk
    if (rec.len > copy_len) {
        ring_read(entry, NULL, rec.len - copy_len); // caller's buffer was too small, discard the rest
    }

    entry->used -= sizeof(rec) + rec.len; // update to keep track of used space

    // return the info
    *src_ip = rec.src_ip;
    *src_port = rec.src_port;
    return (int)copy_len;
}
