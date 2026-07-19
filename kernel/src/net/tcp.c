#include "net/tcp.h"
#include "net/ipv4.h"
#include "net/pkt.h"
#include "net/checksum.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "terminal/terminal.h"
#include <stdint.h>
#include <stdbool.h>

#define TCP_MAX_PAYLOAD    1460
#define TCP_DEFAULT_WINDOW 8192

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol; // 6 (tcp)
    uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_header_t;

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *segment, size_t len) {
    tcp_pseudo_header_t ph = {
        .src_ip     = htonl(src_ip),
        .dst_ip     = htonl(dst_ip),
        .zero       = 0,
        .protocol   = IPV4_PROTO_TCP,
        .tcp_length = htons((uint16_t)len),
    };
    uint32_t sum = checksum_partial(0, &ph, sizeof(ph));
    sum = checksum_partial(sum, segment, len);
    return checksum_fold(sum);
}

// tcb
typedef struct {
    bool          in_use;
    net_device_t *dev;
    tcp_state_t   state;
    uint32_t      src_ip, dst_ip;
    uint16_t      src_port, dst_port;

    // us
    uint32_t      iss;      // initial send sequence number
    uint32_t      snd_una;  // oldest byte we sent that hasnt been acked
    uint32_t      snd_nxt;  // next sequence number we send

    // them
    uint32_t      irs;      // peers initial sequence number
    uint32_t      rcv_nxt;  // next sequence number we expect from the peer
    uint16_t      rcv_wnd;  // our window size
} tcp_connection_t;

#define TCP_MAX_CONNS     8
#define TCP_MAX_LISTENERS 4

typedef struct {
    bool          in_use;
    net_device_t *dev;
    uint32_t      src_ip;
    uint16_t      src_port;
} tcp_listener_t;

static tcp_listener_t listeners[TCP_MAX_LISTENERS];
static tcp_connection_t conns[TCP_MAX_CONNS];

static tcp_connection_t *conn_alloc(void) {
    for (int i=0; i<TCP_MAX_CONNS; i++)
        if (!conns[i].in_use) {
            memset(&conns[i], 0, sizeof(conns[i])); // clear
            conns[i].in_use = true;
            return &conns[i];
        }
    return NULL; // full
}

static tcp_listener_t *listener_alloc(void) {
    for (int i=0; i<TCP_MAX_LISTENERS; i++)
        if (!listeners[i].in_use)
            return &listeners[i];
    return NULL; // full
}

// an existing connection matching the full quad
static tcp_connection_t *find_connection(uint32_t peer_ip, uint16_t peer_port,
                                         uint32_t local_ip, uint16_t local_port) {
    for (int i=0; i<TCP_MAX_CONNS; i++) {
        tcp_connection_t *c = &conns[i];
        if (c->in_use &&
            c->dst_ip==peer_ip && c->dst_port==peer_port &&
            c->src_ip==local_ip && c->src_port==local_port)
            return c;
    }
    return NULL;
}

// a listener on the given local port
static tcp_listener_t *find_listener(uint16_t local_port) {
    for (int i=0; i<TCP_MAX_LISTENERS; i++)
        if (listeners[i].in_use && listeners[i].src_port==local_port)
            return &listeners[i];
    return NULL;
}

// pick an initial send sequence number 
static uint32_t choose_sequence_no(void) {
    return 0; // TODO randomize
}

int tcp_listen(net_device_t *dev, uint16_t port) {
    if (find_listener(port)) return -1; // already listening
    tcp_listener_t *l = listener_alloc();
    if (!l) return -1; // pool full

    l->in_use = true;
    l->dev = dev;
    l->src_ip = dev->ip;
    l->src_port = port;
    return 0;
}

static tcp_connection_t *connection_create(
        net_device_t *dev,
        uint32_t peer_ip, uint16_t peer_port,
        uint32_t local_ip, uint16_t local_port,
        uint32_t seq, uint32_t ack
        ) {

    tcp_connection_t *c = conn_alloc();
    if (!c) return NULL; // pool full
    c->dev = dev;
    c->src_ip = local_ip;
    c->dst_ip = peer_ip;
    c->src_port = local_port;
    c->dst_port = peer_port;

    c->iss = seq;
    c->snd_una = seq;
    c->snd_nxt = seq;

    c->irs = ack-1; // ack = peer_seq + 1, so irs = ack - 1
    c->rcv_nxt = ack;
    c->rcv_wnd = TCP_DEFAULT_WINDOW;

    return c; // caller drives the state transition
}

static int transmit(tcp_connection_t *c, uint8_t flags) {
    uint8_t p_buf[PBUF_HEADROOM];
    pbuf_t p;
    pbuf_init(&p, p_buf, PBUF_HEADROOM, PBUF_HEADROOM);

    tcp_header_t *header = (tcp_header_t *)pbuf_add_header(&p, sizeof(tcp_header_t));
    header->src_port    = htons(c->src_port);
    header->dst_port    = htons(c->dst_port);
    header->seq         = htonl(c->snd_nxt);
    header->ack         = htonl((flags&ACK) ? c->rcv_nxt : 0);
    header->data_offset = (sizeof(tcp_header_t)/4)<<4;
    header->flags       = flags;
    header->window      = htons(c->rcv_wnd);
    header->urgent_ptr  = 0;
    header->checksum    = 0;
    header->checksum    = tcp_checksum(c->src_ip, c->dst_ip, header, sizeof(tcp_header_t));

    return ipv4_send(c->dev, c->dst_ip, IPV4_PROTO_TCP, &p);
}

static void handle_listen(const tcp_header_t *header, uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    tcp_listener_t *l = find_listener(dst_port);
    if (!l) return; // nobody -> drop

    if (header->flags&RST) return;

    // TODO reply RST <SEQ=SEG.ACK><CTL=RST>
    if (header->flags&ACK) {
        return;
    }

    if (header->flags&SYN) {
        kprintf("we got a SYN\n");

        uint32_t ack = ntohl(header->seq)+1;
        tcp_connection_t *c = connection_create(
                l->dev,
                src_ip, src_port,
                l->src_ip, l->src_port,
                choose_sequence_no(), ack);
        if (!c) return; // pool full

        transmit(c, SYN|ACK);
        c->snd_nxt++; // our SYN consumes one sequence number

        kprintf("we sent a SYN ACK\n");
        c->state = TCP_SYN_RECEIVED;
    }
}

void tcp_process(net_device_t *dev, const void *buffer, size_t len, uint32_t src_ip) {
    if (len < sizeof(tcp_header_t)) return;
    const tcp_header_t *header = (const tcp_header_t *)buffer;

    size_t header_len = (header->data_offset>>4)*4;
    if (header_len < sizeof(tcp_header_t) || header_len > len) return; // bad data offset

    size_t data_len = len-header_len;

    uint16_t src_port = ntohs(header->src_port);
    uint16_t dst_port = ntohs(header->dst_port);

    kprintf("tcp: %d -> %d flags=%x seq=%x len=%d\n",
            src_port, dst_port, header->flags, ntohl(header->seq), (int)data_len);

    tcp_connection_t *c = find_connection(src_ip, src_port, dev->ip, dst_port);
    if (!c) {
        handle_listen(header, src_ip, src_port, dst_port);
        return;
    }

    uint32_t seg_seq = ntohl(header->seq);
    uint32_t seg_ack = ntohl(header->ack);

    // First, check sequence number
    if (seg_seq != c->rcv_nxt) return;

    switch (c->state) {
        case TCP_SYN_RECEIVED:
            // Second, check the RST bit
            if (header->flags&RST) {
                c->in_use = false;
                // here the spec tells us to revert to listener state,
                // but we have a seperate listener list so its fine we just delete this
                return;
            }

            // must be an ACK
            if (!(header->flags&ACK))
                return;

            // does it ack our SYN? valid final ack has seg_ack == snd_nxt
            if (seg_ack < c->snd_una || seg_ack > c->snd_nxt) return;

            kprintf("we got an ACK\n");

            c->snd_una = seg_ack;
            c->state = TCP_ESTABLISHED; // handshake is done
            kprintf("tcp: connection established\n");

            break;

        case TCP_ESTABLISHED:
            break;
        default:
            break;
    }
}
