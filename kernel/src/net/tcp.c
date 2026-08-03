#include "net/tcp.h"
#include "net/ipv4.h"
#include "net/block.h"
#include "net/random.h"
#include "net/checksum.h"
#include "net/byteorder.h"
#include "arch/tsc.h"
#include <stddef.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define TCP_MTU 1500
#define IPV4_HDR_LEN 20
#define TCP_HDR_LEN sizeof(tcp_header_t)

#define TCP_DEFAULT_WINDOW 8192

typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_header_t;

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void *segment, size_t len) {
    tcp_pseudo_header_t ph = {
        .src_ip = htonl(src_ip),
        .dst_ip = htonl(dst_ip),
        .zero = 0,
        .protocol = IPV4_PROTO_TCP,
        .tcp_length = htons((uint16_t)len),
    };
    uint32_t sum = checksum_partial(0, &ph, sizeof(ph));
    sum = checksum_partial(sum, segment, len);
    return checksum_fold(sum);
}

#define NTCB 16384 // 2^14

static tcpcb_t tcbs[NTCB];
static pool_t tcb_pool;

#define TCB_HASH_SIZE 16384 // 2^14

static hnode_t *tcb_table[TCB_HASH_SIZE];
static uint32_t tcb_hash_secret;

static uint32_t tcb_hash_key(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
    uint32_t h=tcb_hash_secret;
    h^=local_ip;
    h^=remote_ip;
    h^=((uint32_t)local_port<<16) | remote_port;
    return h%TCB_HASH_SIZE;
}

tcpcb_t *tcb_alloc(net_device_t *dev, uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
    tcpcb_t *tcb = pool_alloc(&tcb_pool);
    if (tcb==NULL)
        return NULL;

    *tcb = (tcpcb_t){0};
    tcb->t_state = CLOSED;
    qinit(&tcb->sndq, TCP_DEFAULT_WINDOW);

    tcb->dev = dev;
    tcb->local_ip = local_ip;
    tcb->remote_ip = remote_ip;
    tcb->local_port = local_port;
    tcb->remote_port = remote_port;

    uint32_t iss = rand32();
    tcb->iss = iss;
    tcb->snd_una = iss;
    tcb->snd_nxt = iss; // not iss+1 because we havent sent yet
    tcb->snd_max = iss;

    tcb->snd_wnd = TCP_DEFAULT_WINDOW; // no segment seen yet to read a real value from
    tcb->t_mss = TCP_MTU-IPV4_HDR_LEN-TCP_HDR_LEN;

    return tcb;
}

void tcb_free(tcpcb_t *tcb) {
    pool_free(&tcb_pool, tcb);
}

void tcb_insert(tcpcb_t *tcb) {
    uint32_t key = tcb_hash_key(tcb->local_ip, tcb->remote_ip, tcb->local_port, tcb->remote_port);
    hnode_insert(&tcb_table[key], &tcb->hnode);
}

void tcb_remove(tcpcb_t *tcb) {
    uint32_t key = tcb_hash_key(tcb->local_ip, tcb->remote_ip, tcb->local_port, tcb->remote_port);
    hnode_remove(&tcb_table[key], &tcb->hnode);
}

tcpcb_t *tcb_lookup(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
    uint32_t key = tcb_hash_key(local_ip, remote_ip, local_port, remote_port);

    for (hnode_t *n = tcb_table[key]; n!=NULL; n=n->next) {
        tcpcb_t *tcb = container_of(n, tcpcb_t, hnode);
        if (tcb->local_ip==local_ip && tcb->remote_ip==remote_ip &&
            tcb->local_port==local_port && tcb->remote_port==remote_port)
            return tcb;
    }

    return NULL;
}

#define NLISTEN 64 // 2^6
#define LISTENER_HASH_SIZE 32 // 2^5

static listener_t listeners[NLISTEN];
static pool_t listener_pool;

static hnode_t *listener_table[LISTENER_HASH_SIZE];

listener_t *listener_create(uint16_t port) {
    if (listener_lookup(port)!=NULL)
        return NULL;

    listener_t *l = pool_alloc(&listener_pool);
    if (l==NULL)
        return NULL;

    *l = (listener_t){0};
    l->port = port;

    hnode_insert(&listener_table[port % LISTENER_HASH_SIZE], &l->hnode);

    return l;
}

void listener_free(listener_t *l) {
    hnode_remove(&listener_table[l->port % LISTENER_HASH_SIZE], &l->hnode);
    pool_free(&listener_pool, l);
}

listener_t *listener_lookup(uint16_t port) {
    for (hnode_t *n = listener_table[port % LISTENER_HASH_SIZE]; n!=NULL; n=n->next) {
        listener_t *l = container_of(n, listener_t, hnode);
        if (l->port==port)
            return l;
    }

    return NULL;
}

int tcp_listen(uint16_t port) {
    return listener_create(port) != NULL ? 0 : -1;
}

#define EPHEMERAL_START 32768
#define EPHEMERAL_END 65535
#define PORT_ALLOC_TRIES 64

uint16_t port_alloc(uint32_t local_ip, uint32_t remote_ip, uint16_t remote_port) {
    for (int i=0; i<PORT_ALLOC_TRIES; i++) {
        uint16_t port = EPHEMERAL_START+(rand32()%(EPHEMERAL_END-EPHEMERAL_START));
        if (tcb_lookup(local_ip,remote_ip,port,remote_port)==NULL)
            return port;
    }
    return 0;
}

void tcp_output(tcpcb_t *tcb) {
    if (tcb->t_state==CLOSED || tcb->t_state==LISTEN)
        return;

    uint8_t flags = ACK;
    uint32_t owed = qlen(&tcb->sndq) + tcb->flgcnt;

    switch (tcb->t_state) {
        case SYN_RECEIVED:
            if (tcb->snd_nxt==tcb->iss) // SYN ACK the SYN we just got
                flags |= SYN;
            owed = tcb->flgcnt; // never piggyback data on a SYN-ACK - real clients reject it (syncookie interop)
            break;
        default:
            break;
    }

    uint32_t inflight = tcb->snd_nxt-tcb->snd_una;
    uint32_t unsent = owed-inflight;

    uint32_t len = MIN(MIN(tcb->snd_wnd-inflight, unsent), tcb->t_mss);

    block_t *b = block_alloc(0, HDR_TCP);
    if (b==NULL)
        return;

    tcp_header_t *out = (tcp_header_t *)block_push(b, sizeof(tcp_header_t));
    out->src_port = htons(tcb->local_port);
    out->dst_port = htons(tcb->remote_port);
    out->seq = htonl(tcb->snd_nxt);
    out->ack = htonl(tcb->rcv_nxt);
    out->data_offset = 5<<4;
    out->flags = flags;
    out->window = htons(TCP_DEFAULT_WINDOW);
    out->urgent_ptr = 0;
    out->checksum = 0;
    out->checksum = tcp_checksum(tcb->local_ip, tcb->remote_ip, out, sizeof(tcp_header_t));

    tcb->snd_nxt += len;
    if (tcb->snd_nxt > tcb->snd_max)
        tcb->snd_max = tcb->snd_nxt;

    // TODO arm retransmit timer

    ipv4_send(tcb->dev, tcb->remote_ip, IPV4_PROTO_TCP, b);
}

// TODO reply RST for listener

void tcp_input(net_device_t *dev, const void *data, size_t len, uint32_t src_ip) {
    if (len<sizeof(tcp_header_t))
        return;
    const tcp_header_t *seg = (const tcp_header_t *)data;

    if (tcp_checksum(dev->ip, src_ip, seg, len) != 0)
        return;

    uint16_t local_port = ntohs(seg->dst_port);
    uint16_t remote_port = ntohs(seg->src_port);

    tcpcb_t *tcb = tcb_lookup(dev->ip, src_ip, local_port, remote_port);
    if (tcb==NULL) {
        listener_t *l = listener_lookup(local_port);
        if (l==NULL)
            return; // TODO RST reply <SEQ=SEG.ACK><CTL=RST>

        // An incoming RST should be ignored. Return.
        if (seg->flags & RST)
            return;

        if (seg->flags & ACK){
            // TODO RST reply <SEQ=SEG.ACK><CTL=RST>
            return;
        }

        if (seg->flags & SYN){
            // TODO check security
            // TODO check prc

            // no data on a bare SYN is queued/processed, same as linux

            tcb = tcb_alloc(dev, dev->ip, src_ip, local_port, remote_port);
            if (tcb==NULL)
                return;

            uint32_t seq=ntohl(seg->seq);
            tcb->irs = seq;
            tcb->rcv_nxt = seq+1;

            tcb->snd_wnd = ntohs(seg->window); // overrides tcb_alloc's placeholder now that we have a real segment
            tcb->flgcnt = 1; // the SYN we're about to send

            tcb->t_state = SYN_RECEIVED;

            tcb_insert(tcb);

            tcp_output(tcb);

            return;
        }

        return;

    } else {
        uint32_t seg_seq = ntohl(seg->seq);

        // First, check sequence number
        if (seg_seq != tcb->rcv_nxt) {
            if (!(seg->flags & RST))
                tcp_output(tcb);
            return; // we drop out of order stuff for now
        }

        // fifth, check the ACK field
        if (!(seg->flags & ACK))
            return;

        uint32_t seg_ack = ntohl(seg->ack);

        switch (tcb->t_state) {
            case SYN_RECEIVED:
                // If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state and continue processing.
                if (!(SEQ_LEQ(tcb->snd_una, seg_ack) && SEQ_LEQ(seg_ack, tcb->snd_nxt))) {
                    // TODO RST reply <SEQ=SEG.ACK><CTL=RST>
                    return;
                }
                tcb->t_state = ESTABLISHED;
                break;
            default:
                break;
        }

        // advance snd_una, discarding whatever this ack covers
        // if it covers more than that the rest was a flag (SYN/FIN) rather than a byte
        uint32_t acked = seg_ack - tcb->snd_una;
        uint32_t data_acked = MIN(acked, qlen(&tcb->sndq));
        qdiscard(&tcb->sndq, data_acked);
        if (data_acked<acked)
            tcb->flgcnt--;
        tcb->snd_una = seg_ack;

    }
}

void tcp_init(void) {
    pool_init(&tcb_pool, tcbs, sizeof(tcpcb_t), NTCB);
    pool_init(&listener_pool, listeners, sizeof(listener_t), NLISTEN);

    srand((uint32_t)rdtsc());
    tcb_hash_secret=rand32();
}
