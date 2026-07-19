#include "net/udp.h"
#include "net/ipv4.h"
#include "net/pkt.h"
#include "net/byteorder.h"
#include "sched/task.h"
#include "lib/string.h"
#include <stdint.h>

#define UDP_MAX_PAYLOAD 1472

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;   // header + payload
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

#define UDP_BIND_TABLE_SIZE 16
#define UDP_RING_SIZE       8192

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
} udp_record_t;

typedef struct {
    uint16_t  port;
    int       in_use;
    uint8_t   ring[UDP_RING_SIZE];
    unsigned  write_pos;
    unsigned  read_pos;
    SEMAPHORE sem; // # of stuff to read
} udp_bind_entry_t;
// used = write_pos-read_pos
// free = UDP_RING_SIZE-used

static udp_bind_entry_t udp_bind_table[UDP_BIND_TABLE_SIZE];

// push to ring buffer
static void ring_write(udp_bind_entry_t *e, const void *src, unsigned len) {
    const uint8_t *data = src;
    unsigned start = e->write_pos%UDP_RING_SIZE;
    unsigned first_len = UDP_RING_SIZE-start; // distance from start to end in bytes
    if (first_len>len) first_len=len;
    memcpy(e->ring+start, data, first_len); //from our start to actual end
    memcpy(e->ring, data+first_len, len-first_len); //wrap (from actual start to our end)
    e->write_pos += len;
}

// pop from ring buffer
static void ring_read(udp_bind_entry_t *e, void *dst, unsigned len) {
    uint8_t *data = dst;
    unsigned start = e->read_pos%UDP_RING_SIZE;
    unsigned first_len = UDP_RING_SIZE-start; // distance from start to end in bytes
    if (first_len>len) first_len=len;
    memcpy(data, e->ring+start, first_len); //from our start to actual end
    memcpy(data+first_len, e->ring, len-first_len); //wrap (from actual start to our end)
    e->read_pos += len;
}

int udp_bind(uint16_t port) {
    // already bound?
    for (int i=0; i<UDP_BIND_TABLE_SIZE; i++)
        if (udp_bind_table[i].in_use && udp_bind_table[i].port==port)
            return -1;

    // find a free slot
    for (int i=0; i<UDP_BIND_TABLE_SIZE; i++) {
        if (!udp_bind_table[i].in_use) {
            udp_bind_table[i].port = port;
            udp_bind_table[i].write_pos = 0;
            udp_bind_table[i].read_pos = 0;
            semaphore_init(&udp_bind_table[i].sem, 0);
            udp_bind_table[i].in_use = 1;
            return 0;
        }
    }
    return -1; // table full
}

// drainer task calls this, this processes the task and puts it into the array
void udp_process(const void *buffer, size_t len, uint32_t src_ip) {
    if (len < sizeof(udp_header_t)) return;
    const udp_header_t *header = (const udp_header_t *)buffer;

    uint16_t dst_port = ntohs(header->dst_port);
    unsigned data_len = len-sizeof(udp_header_t);

    // find the port; if nobodys bound, drop it
    udp_bind_entry_t *e = NULL;
    for (int i=0; i<UDP_BIND_TABLE_SIZE; i++) {
        if (udp_bind_table[i].in_use && udp_bind_table[i].port == dst_port) {
            e = &udp_bind_table[i];
            break;
        }
    }
    if (!e) return;

    unsigned record_size = sizeof(udp_record_t)+data_len;
    unsigned used = e->write_pos - e->read_pos;
    if (used+record_size>UDP_RING_SIZE) // can it fit? no=drop
        return;

    // it can fit, add it
    udp_record_t rec = {
        .src_ip   = src_ip,
        .src_port = ntohs(header->src_port),
        .len      = (uint16_t)data_len,
    };
    ring_write(e, &rec, sizeof(rec));
    ring_write(e, (const uint8_t *)buffer + sizeof(udp_header_t), data_len);

    semaphore_release(&e->sem);
}

int udp_send(net_device_t *dev, uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const void *payload, size_t len) {
    if (len>UDP_MAX_PAYLOAD) return -1;

    uint8_t p_buf[PBUF_HEADROOM + sizeof(udp_header_t) + UDP_MAX_PAYLOAD];
    pbuf_t p;
    pbuf_init(&p, p_buf, PBUF_HEADROOM + len, PBUF_HEADROOM); // sz = payload only; header prepended below

    memcpy(p.data, payload, len);

    udp_header_t *header = (udp_header_t *)pbuf_add_header(&p, sizeof(udp_header_t));
    header->src_port = htons(src_port);
    header->dst_port = htons(dst_port);
    header->length = htons(sizeof(udp_header_t)+len);
    header->checksum = 0;

    return ipv4_send(dev, dst_ip, IPV4_PROTO_UDP, &p);
}

int udp_recv(uint16_t port, uint32_t *src_ip, uint16_t *src_port, void *buf, size_t buflen) {
    // find the bound entry
    udp_bind_entry_t *e = NULL;
    for (int i=0; i<UDP_BIND_TABLE_SIZE; i++) {
        if (udp_bind_table[i].in_use && udp_bind_table[i].port==port) {
            e = &udp_bind_table[i];
            break;
        }
    }
    if (!e) return -1; // port was never bound

    semaphore_acquire(&e->sem); // blocks until something is in the q

    // read the record header, then its data
    udp_record_t rec;
    ring_read(e, &rec, sizeof(rec));

    // udp is message-oriented: copy what fits, discard the rest of this datagram
    unsigned copy_len = rec.len < buflen ? rec.len : (unsigned)buflen;
    ring_read(e, buf, copy_len);
    e->read_pos += rec.len-copy_len; // skip the truncated remainder

    if (src_ip) *src_ip = rec.src_ip;
    if (src_port) *src_port = rec.src_port;
    return (int)copy_len;
}
