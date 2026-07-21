#include "net/tcp.h"
#include "net/ipv4.h"
#include "net/pkt.h"
#include "net/checksum.h"
#include "net/byteorder.h"
#include "lib/string.h"
#include "sched/task.h"
#include "arch/pit.h"
#include "terminal/terminal.h"
#include <stdint.h>
#include <stdbool.h>

#define TCP_MAX_PAYLOAD 1460
#define TCP_DEFAULT_WINDOW 8192
#define TCP_RCV_BUF 8192
#define TCP_SND_BUF 8192

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define TCP_RETRANSMIT_NS (1000ULL*1000000) // 1000ms

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
typedef struct tcp_connection {
    bool          in_use;
    net_device_t *dev;
    tcp_state_t   state;
    uint32_t      src_ip, dst_ip;
    uint16_t      src_port, dst_port;

    // us
    //uint32_t    iss;      // initial send sequence number
    uint32_t      snd_una;  // oldest byte we sent that hasnt been acked
    uint32_t      snd_nxt;  // next sequence number we send
    uint16_t      snd_wnd;  // peers advertised window
    uint32_t      snd_wl1;  // seq of segment that last updated snd_wnd
    uint32_t      snd_wl2;  // ack of segment that last updated snd_wnd

    // them
    //uint32_t    irs;      // peers initial sequence number
    uint32_t      rcv_nxt;  // next sequence number we expect from the peer
    uint16_t      rcv_wnd;  // our window size

    uint8_t       rcv_ring[TCP_RCV_BUF];
    unsigned      rcv_write, rcv_read; 
    // used = write-read, free = TCP_RCV_BUF-used

    SEMAPHORE     rcv_sem;
    bool          rcv_closed;

    SEMAPHORE     connect_sem; // tcp_connect() blocks on this until the handshake completes

    uint8_t       snd_ring[TCP_SND_BUF];
    unsigned      snd_write;
    // snd_una is the snd_read counterpart

    uint64_t      retransmit_deadline;

    struct tcp_connection *accept_next; // next ready connection in listeners accept queue
} tcp_connection_t;

#define TCP_MAX_CONNS     8
#define TCP_MAX_LISTENERS 4

typedef struct {
    bool in_use;
    net_device_t *dev;
    uint32_t src_ip;
    uint16_t src_port;
    tcp_connection_t *accept_head, *accept_tail; // FIFO of completed connections
    SEMAPHORE accept_sem; // released once per queued connection
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

// append a completed connection to its listener's accept queue
static void accept_push(tcp_listener_t *l, tcp_connection_t *c) {
    c->accept_next = NULL;
    if (l->accept_tail) 
        l->accept_tail->accept_next = c;
    else 
        l->accept_head = c;
    l->accept_tail = c;
    semaphore_release(&l->accept_sem);
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
    l->accept_head = NULL;
    l->accept_tail = NULL;
    semaphore_init(&l->accept_sem, 0);
    return 0;
}

// push bytes into the receive ring
static void rcv_ring_write(tcp_connection_t *c, const void *src, unsigned len) {
    const uint8_t *data = src;
    uint8_t *ring = c->rcv_ring;

    unsigned start = c->rcv_write%TCP_RCV_BUF;
    unsigned first_len = MIN(TCP_RCV_BUF-start, len); // distance from start to end, clamped
    memcpy(ring+start, data, first_len); // from our start to actual end
    memcpy(ring, data+first_len, len-first_len); // wrap
    c->rcv_write += len;
}

// pop bytes from the receive ring
static void rcv_ring_read(tcp_connection_t *c, void *dst, unsigned len) {
    uint8_t *data = dst;
    uint8_t *ring = c->rcv_ring;

    unsigned start = c->rcv_read%TCP_RCV_BUF;
    unsigned first_len = MIN(TCP_RCV_BUF-start, len); // distance from start to end, clamped
    memcpy(data, ring+start, first_len); // from our start to actual end
    memcpy(data+first_len, ring, len-first_len); // wrap
    c->rcv_read += len;
}

// push bytes into the send ring (queued for sending)
static void snd_ring_write(tcp_connection_t *c, const void *src, unsigned len) {
    const uint8_t *data = src;
    uint8_t *ring = c->snd_ring;

    unsigned start = c->snd_write%TCP_SND_BUF;
    unsigned first_len = MIN(TCP_SND_BUF-start, len); // distance from start to end, clamped
    memcpy(ring+start, data, first_len); // from our start to actual end
    memcpy(ring, data+first_len, len-first_len); // wrap
    c->snd_write += len;
}

// seq is either una (peek for retransmit) or nxt (peek for transmit)
static void snd_ring_peek(tcp_connection_t *c, unsigned seq, void *dst, unsigned len) {
    uint8_t *data = dst;
    uint8_t *ring = c->snd_ring;

    unsigned start = seq%TCP_SND_BUF;
    unsigned first_len = MIN(TCP_SND_BUF-start, len); // distance from start to end, clamped
    memcpy(data, ring+start, first_len); // from our start to actual end
    memcpy(data+first_len, ring, len-first_len); // wrap
}

static unsigned rcv_used(tcp_connection_t *c) { return c->rcv_write - c->rcv_read; }
static unsigned rcv_free(tcp_connection_t *c) { return TCP_RCV_BUF - rcv_used(c); }

static unsigned snd_used(tcp_connection_t *c) { return c->snd_write - c->snd_una; }
static unsigned snd_free(tcp_connection_t *c) { return TCP_SND_BUF - snd_used(c); }

static void tcp_output(tcp_connection_t *c); // send queued unsent data

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

// send len bytes from the send ring starting at sequence number seq
static int transmit_data(tcp_connection_t *c, uint8_t flags, unsigned seq, unsigned len) {
    uint8_t p_buf[PBUF_HEADROOM + TCP_MAX_PAYLOAD];
    pbuf_t p;
    pbuf_init(&p, p_buf, PBUF_HEADROOM + len, PBUF_HEADROOM);

    snd_ring_peek(c, seq, p.data, len); // copy payload into the pbuf

    tcp_header_t *header = (tcp_header_t *)pbuf_add_header(&p, sizeof(tcp_header_t));
    header->src_port    = htons(c->src_port);
    header->dst_port    = htons(c->dst_port);
    header->seq         = htonl(seq); // seq of the first payload byte
    header->ack         = htonl(c->rcv_nxt);
    header->data_offset = (sizeof(tcp_header_t)/4)<<4;
    header->flags       = flags;
    header->window      = htons(c->rcv_wnd);
    header->urgent_ptr  = 0;
    header->checksum    = 0;
    // header and payload are contiguous in the pbuf, so checksum both
    header->checksum    = tcp_checksum(c->src_ip, c->dst_ip, header, sizeof(tcp_header_t) + len);

    return ipv4_send(c->dev, c->dst_ip, IPV4_PROTO_TCP, &p);
}

static void handle_listen(const tcp_header_t *header, uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    tcp_listener_t *l = find_listener(dst_port);
    if (!l) return; // nobody -> drop

    // First, check for a RST
    if (header->flags&RST) return;

    // Second, check for an ACK
    if (header->flags&ACK) {
        // TODO reply RST <SEQ=SEG.ACK><CTL=RST>
        return;
    }

    // Third, check for a SYN
    if (header->flags&SYN) {
        kprintf("we got a SYN\n");

        uint32_t ack = ntohl(header->seq)+1; // if their SEQ was 100 our ACK is 101
        uint32_t seq = choose_sequence_no();

        // init our connection
        tcp_connection_t *c = conn_alloc();
        if (!c) return; // pool full, its ok to drop as they can resend their syn

        c->dev = l->dev;

        c->src_ip = l->src_ip;
        c->dst_ip = src_ip;
        c->src_port = l->src_port;
        c->dst_port = src_port;

        // c->iss = seq;
        c->snd_una = seq;
        c->snd_nxt = seq;

        // c->irs = ack-1;
        c->rcv_nxt = ack;
        c->rcv_wnd = TCP_RCV_BUF;
        semaphore_init(&c->rcv_sem, 0);

        transmit(c, SYN|ACK);
        c->snd_nxt++;
        c->snd_write = c->snd_nxt;

        kprintf("we sent a SYN ACK\n");
        c->state = TCP_SYN_RECEIVED;
    }
}

// actively open a connection, send SYN, block till handshake completes or fails
tcp_connection_t *tcp_connect(net_device_t *dev, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port) {
    tcp_connection_t *c = conn_alloc();
    if (!c) return NULL; // pool full

    c->dev = dev;
    c->src_ip = dev->ip;
    c->dst_ip = dst_ip;
    c->src_port = src_port;
    c->dst_port = dst_port;

    uint32_t seq = choose_sequence_no();
    c->snd_una = seq;
    c->snd_nxt = seq;
    c->rcv_wnd = TCP_RCV_BUF;
    semaphore_init(&c->rcv_sem, 0);
    semaphore_init(&c->connect_sem, 0);

    transmit(c, SYN);
    c->snd_nxt++;
    c->snd_write = c->snd_nxt; // send ring shares the sequence-number space
    c->state = TCP_SYN_SENT;

    semaphore_acquire(&c->connect_sem); // blocks until the SYN_SENT handler completes the handshake

    if (c->state != TCP_ESTABLISHED) { // refused or failed
        c->in_use = false;
        return NULL;
    }
    return c;
}

void tcp_process(net_device_t *dev, const void *buffer, size_t len, uint32_t src_ip) {
    if (len < sizeof(tcp_header_t)) return;
    const tcp_header_t *header = (const tcp_header_t *)buffer;

    size_t header_len = (header->data_offset>>4)*4;
    if (header_len < sizeof(tcp_header_t) || header_len > len) return; // bad data offset

    size_t data_len = len-header_len;

    // verify the checksum
    if (tcp_checksum(src_ip,dev->ip,header,len)!=0)
        return; // corrupted

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

    if (c->state==TCP_SYN_SENT) {
        // First, check the ACK bit
        if ((header->flags&ACK) && seg_ack != c->snd_nxt) {
            if (!(header->flags&RST)) { // unless the RST bit is set, send <SEQ=SEG.ACK><CTL=RST>
                uint32_t saved = c->snd_nxt;
                c->snd_nxt = seg_ack;
                transmit(c, RST);
                c->snd_nxt = saved;
            }
            return;
        }

        // Second, check the RST bit
        if (header->flags&RST) {
            if (header->flags&ACK) {
                c->in_use = false;
                semaphore_release(&c->connect_sem); // wake connect, it should realise we failed
            }
            return;
        }

        // Fourth, check the SYN bit
        if (header->flags&SYN) {
            c->rcv_nxt = seg_seq+1; // peers ISS+1
            if (header->flags&ACK) {
                c->snd_una = seg_ack;
                c->snd_wnd = ntohs(header->window);
                c->state = TCP_ESTABLISHED;
                transmit(c, ACK); // final handshake ack
                semaphore_release(&c->connect_sem);
            }
        } 
        // TODO simultaneosu open
        // TODO it it doesnt fail as a result of RST ACK we just are sleeping forever
        // instead we need to retry with backoff and then give up at some point
        return;
    }

    // First, check sequence number
    if (seg_seq != c->rcv_nxt){
        if (!(header->flags&RST))
            transmit(c, ACK);
        return; // we drop out of order stuff for now
    }

    switch (c->state) {
        case TCP_SYN_RECEIVED:
            // Second, check the RST bit
            if (header->flags&RST) { // return to listen
                c->in_use = false;
                return;
            }

            // Fourth, check the SYN bit
            if (header->flags&SYN) { // return to listen
                c->in_use = false;
                return;
            }
            
            // Fifth, check the ACK field
            if (!(header->flags&ACK))
                return;
            // ack is on

            if (!(c->snd_una < seg_ack && seg_ack <= c->snd_nxt)) {
                // send reset segment <SEQ=SEG.ACK><CTL=RST>
                uint32_t saved = c->snd_nxt;
                c->snd_nxt = seg_ack;
                transmit(c, RST);
                c->snd_nxt = saved;
                return;
            }

            c->snd_una = seg_ack; // they acked our syn ack when they said ack
            c->snd_wnd = ntohs(header->window);
            c->snd_wl1 = seg_seq;
            c->snd_wl2 = seg_ack;
            c->state = TCP_ESTABLISHED; // handshake is done

            // append it to the queue
            tcp_listener_t *l = find_listener(c->src_port);
            if (l) accept_push(l, c);

            kprintf("we got an ACK\n");
            kprintf("tcp: connection established\n");
            break;

        case TCP_ESTABLISHED:
            // Second, check the RST bit
            if (header->flags&RST) {
                c->in_use = false;
                return;
            }

            // Fifth, check the ACK field
            if (!(header->flags&ACK))
                return;

            // If SND.UNA < SEG.ACK =< SND.NXT, then set SND.UNA <- SEG.ACK
            if (c->snd_una < seg_ack && seg_ack <= c->snd_nxt) {
                c->snd_una = seg_ack;

                // una advanced, if nothing is unacked, dont retransmit, else, restart the timer
                // if (c->snd_una == c->snd_nxt)
                //     c->retransmit_deadline = 0;
                // else
                //     c->retransmit_deadline = get_time_since_boot()+TCP_RETRANSMIT_NS;
            }

            // If the ACK is a duplicate (SEG.ACK =< SND.UNA), it can be ignored.

            // If the ACK acks something not yet sent (SEG.ACK > SND.NXT),
            // then send an ACK, drop the segment, and return.
            else if (seg_ack > c->snd_nxt) {
                transmit(c, ACK);
                return;
            }

            // If SND.UNA =< SEG.ACK =< SND.NXT, the send window should be updated.
            if (c->snd_una <= seg_ack && seg_ack <= c->snd_nxt) {
                if (c->snd_wl1 < seg_seq || (c->snd_wl1 == seg_seq && c->snd_wl2 <= seg_ack)) {
                    c->snd_wnd = ntohs(header->window);
                    c->snd_wl1 = seg_seq;
                    c->snd_wl2 = seg_ack;
                }
            }

            // they acked something we sent, they have more space free for us to send stuff
            tcp_output(c);

            if (data_len>0) {
                const uint8_t *payload = (const uint8_t *)header;
                payload+=header_len; // step header_len bytes forward so we are at the payload

                bool was_empty = (rcv_used(c)==0);
                unsigned take = MIN((unsigned)data_len, rcv_free(c)); // take as much as we can
                rcv_ring_write(c, payload, take);

                if (was_empty && take>0)
                    semaphore_release(&c->rcv_sem); // empty -> non-empty: signal readable

                c->rcv_nxt += take;
                c->rcv_wnd = rcv_free(c); // window = free space
                transmit(c, ACK);
            }

            // Eighth, check the FIN bit
            if (header->flags&FIN){
                c->rcv_nxt++;
                transmit(c, ACK);
                c->state = TCP_CLOSE_WAIT;

                c->rcv_closed=true; // no more data will arrive
                semaphore_release(&c->rcv_sem);

                return;
            }
            break;

        case TCP_LAST_ACK:
            // Second, check the RST bit
            if (header->flags&RST) {
                c->in_use = false;
                return;
            }

            // waiting for the peer to ack our FIN
            if ((header->flags&ACK) && seg_ack == c->snd_nxt) {
                c->in_use = false; // closed, free the slot
                kprintf("tcp: connection closed\n");
            }
            return;

        default:
            break;
    }
}

// send whatever is queued and unsent
static void tcp_output(tcp_connection_t *c) {
    while (c->snd_nxt != c->snd_write) {
        unsigned inflight = c->snd_nxt - c->snd_una; // unacked bytes
        // peers window has no more room
        if (inflight >= c->snd_wnd)
            break;
        unsigned window_room = c->snd_wnd-inflight; // how much more the peer will accept

        unsigned send_len = c->snd_write - c->snd_nxt;
        send_len = MIN(send_len, TCP_MAX_PAYLOAD);
        send_len = MIN(send_len, window_room);

        uint8_t flags=ACK;
        if (c->snd_nxt+send_len == c->snd_write) // last chunk
            flags|=PSH;

        transmit_data(c, flags, c->snd_nxt, send_len);

        c->snd_nxt+=send_len;
    }

    // // if there is stuff unacked, in 1s we will retransmit
    // if (c->retransmit_deadline==0 && c->snd_una != c->snd_nxt)
    //     c->retransmit_deadline = get_time_since_boot()+TCP_RETRANSMIT_NS;
}

// queue len bytes for sending; returns bytes accepted, or -1 if the state forbids sending
int tcp_send(tcp_connection_t *c, const void *buf, size_t len) {
    switch (c->state) {
        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT: {
            unsigned take = MIN((unsigned)len, snd_free(c)); // bytes accepted into the ring
            snd_ring_write(c, buf, take); // queue data

            tcp_output(c); // push the newly queued data
            return (int)take;
        }
        default:
            // not yet established, or we are closing -> cannot send
            return -1;
    }
}

// close our sending side: flush queued data, send FIN. returns 0, or -1 if not open
int tcp_close(tcp_connection_t *c) {
    if (c->state != TCP_CLOSE_WAIT) // only the passive-close path for now
        return -1;

    tcp_output(c); // make sure any queued data went out first
    transmit(c, FIN|ACK);
    c->snd_nxt++;
    c->state = TCP_LAST_ACK;
    return 0;
}

// block until data is available, copy up to len bytes out, return the count (0 = EOF)
int tcp_recv(tcp_connection_t *c, void *buf, size_t len) {
    semaphore_acquire(&c->rcv_sem); // blocks until ring non-empty or closed

    lock_stuff();
    unsigned used = rcv_used(c);
    if (used==0) { // woke for EOF, nothing buffered
        unlock_stuff();
        return 0;
    }

    // take all we have, or up to len if it cant fit
    unsigned take = MIN(used, (unsigned)len);
    rcv_ring_read(c, buf, take);
    c->rcv_wnd = rcv_free(c); // window reopens as we drain

    // still data left?
    if (rcv_used(c)!=0 || c->rcv_closed)
        semaphore_release(&c->rcv_sem);

    unlock_stuff();
    return (int)take; // return how many bytes we took
}

// pop the oldest completed connection off the listener's accept queue
static tcp_connection_t *accept_pop(tcp_listener_t *l) {
    tcp_connection_t *c = l->accept_head;
    l->accept_head = c->accept_next;
    if (!l->accept_head)
        l->accept_tail=NULL;
    c->accept_next=NULL;
    return c;
}

// block until a connection completes its handshake, then hand it to the caller
tcp_connection_t *tcp_accept(uint16_t port) {
    tcp_listener_t *l = find_listener(port);
    if (!l) return NULL;

    semaphore_acquire(&l->accept_sem); // blocks till we have a connection

    lock_stuff();
    tcp_connection_t *c = accept_pop(l);
    unlock_stuff();
    return c;
}

// retransmit oldest unacked data
// static void retransmit(tcp_connection_t *c) {
//     unsigned n = c->snd_nxt - c->snd_una; // outstanding unacked bytes
//     unsigned seg = MIN(n, TCP_MAX_PAYLOAD);
//     transmit_data(c, ACK, c->snd_una, seg); // seq is una
//     c->retransmit_deadline = get_time_since_boot()+TCP_RETRANSMIT_NS;
//     kprintf("tcp: retransmit %d bytes at seq %x\n", (int)seg, c->snd_una);
// }

// fire retransmits
// TODO fix possible races/implement callbacks
// void tcp_timer_task(void) {
//     unlock_scheduler();
//     for (;;) {
//         ms_sleep(250);
//         uint64_t now = get_time_since_boot();
//         for (int i=0; i<TCP_MAX_CONNS; i++) {
//             tcp_connection_t *c = &conns[i];
//             if (c->in_use && c->retransmit_deadline != 0 && now >= c->retransmit_deadline)
//                 retransmit(c);
//         }
//     }
// }
