#include "net/tcp.h"
#include "net/ipv4.h"
#include "net/block.h"
#include "net/random.h"
#include "net/checksum.h"
#include "lib/string.h"
#include "net/byteorder.h"
#include "arch/tsc.h"
#include "kernel.h"
#include <stddef.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

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
    tcb->state = CLOSED;
    qinit(&tcb->sndq, TCP_DEFAULT_WINDOW);
    qinit(&tcb->rcvq, TCP_DEFAULT_WINDOW);
    semaphore_init(&tcb->lock, 1);

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

    tcb->snd_wnd = TCP_DEFAULT_WINDOW;
    tcb->mss = TCP_MTU-IPV4_HDR_LEN-TCP_HDR_LEN;

    return tcb;
}

void tcb_free(tcpcb_t *tcb) {
    pool_free(&tcb_pool, tcb);
}

void tcb_close(tcpcb_t *tcb, int error) {
    ASSERT(tcb->lock.current_count>0, "tcb_close called without tcb lock held");

    tcb_remove(tcb); // remove from hashmap

    if (tcb->t_rxt_timer.armed)
        timer_cancel(&tcb->t_rxt_timer);

    qflush(&tcb->sndq);
    qflush(&tcb->rcvq);

    tcb->error = error;

    waitq_broadcast(&tcb->connecting);
    waitq_broadcast(&tcb->readers);
    waitq_broadcast(&tcb->writers);

    tcb->state = CLOSED;
}

void tcb_insert(tcpcb_t *tcb) {
    uint32_t key = tcb_hash_key(tcb->local_ip, tcb->remote_ip, tcb->local_port, tcb->remote_port);
    lock_stuff();
    hnode_insert(&tcb_table[key], &tcb->hnode);
    unlock_stuff();
}

void tcb_remove(tcpcb_t *tcb) {
    uint32_t key = tcb_hash_key(tcb->local_ip, tcb->remote_ip, tcb->local_port, tcb->remote_port);
    lock_stuff();
    hnode_remove(&tcb_table[key], &tcb->hnode);
    unlock_stuff();
}

tcpcb_t *tcb_lookup(uint32_t local_ip, uint32_t remote_ip, uint16_t local_port, uint16_t remote_port) {
    uint32_t key = tcb_hash_key(local_ip, remote_ip, local_port, remote_port);

    lock_stuff();
    for (hnode_t *n = tcb_table[key]; n!=NULL; n=n->next) {
        tcpcb_t *tcb = container_of(n, tcpcb_t, hnode);
        if (tcb->local_ip==local_ip && tcb->remote_ip==remote_ip &&
            tcb->local_port==local_port && tcb->remote_port==remote_port) {
            unlock_stuff();
            return tcb;
        }
    }
    unlock_stuff();

    return NULL;
}

#define NLISTEN 64 // 2^6
#define LISTENER_HASH_SIZE 32 // 2^5
#define MAX_INCOMING 64

static listener_t listeners[NLISTEN];
static pool_t listener_pool;

static hnode_t *listener_table[LISTENER_HASH_SIZE];

listener_t *listener_create(uint16_t port) {
    if (listener_lookup(port)!=NULL) // taken
        return NULL;

    listener_t *l = pool_alloc(&listener_pool);
    if (l==NULL)
        return NULL;

    *l = (listener_t){0};
    l->port = port;
    semaphore_init(&l->lock, 1);

    lock_stuff();
    hnode_insert(&listener_table[port%LISTENER_HASH_SIZE], &l->hnode); // add to hashmap
    unlock_stuff();

    return l;
}

static void listener_try_free(listener_t *l) {
    // caller must hold l->lock
    ASSERT(l->lock.current_count==1, "listener_try_free called without listener lock held");

    if (l->closing && l->refcount==0) {
        release_mutex(&l->lock);
        pool_free(&listener_pool, l);
        return;
    }
    release_mutex(&l->lock);
}

void listener_close(listener_t *l) {
    acquire_mutex(&l->lock);

    l->closing = true;
    waitq_broadcast(&l->accept_waiters); // wakes so stuff doesnt wait forever

    // TODO tear down each queued tcb

    l->accept_head = l->accept_tail = NULL;
    l->nqueued = 0;

    lock_stuff();
    hnode_remove(&listener_table[l->port % LISTENER_HASH_SIZE], &l->hnode);
    unlock_stuff();

    listener_try_free(l); // releases the lock
}

listener_t *listener_lookup(uint16_t port) {
    lock_stuff();
    for (hnode_t *n = listener_table[port % LISTENER_HASH_SIZE]; n!=NULL; n=n->next) {
        listener_t *l = container_of(n, listener_t, hnode);
        if (l->port==port) {
            unlock_stuff();
            return l;
        }
    }
    unlock_stuff();

    return NULL;
}

int tcp_listen(uint16_t port) {
    return listener_create(port)!=NULL ? 0 : -1;
}

tcpcb_t *tcp_accept(listener_t *l) {
    acquire_mutex(&l->lock);
    l->refcount++;

    while (l->nqueued==0 && !l->closing)
        waitq_wait(&l->accept_waiters, &l->lock);

    tcpcb_t *tcb = NULL;
    if (!l->closing) {
        tcb = l->accept_head;
        l->accept_head = tcb->accept_next;
        if (l->accept_head==NULL)
            l->accept_tail = NULL;
        l->nqueued--;
    }

    l->refcount--;
    listener_try_free(l); // releases the lock
    return tcb;
}

#define EPHEMERAL_START 32768
#define EPHEMERAL_END 65535
#define PORT_ALLOC_TRIES 64

// tries 64 times to pick a random port from the range
uint16_t port_alloc(uint32_t local_ip, uint32_t remote_ip, uint16_t remote_port) {
    for (int i=0; i<PORT_ALLOC_TRIES; i++) {
        uint16_t port = EPHEMERAL_START+(rand32()%(EPHEMERAL_END-EPHEMERAL_START));
        if (tcb_lookup(local_ip,remote_ip,port,remote_port)==NULL)
            return port;
    }
    return 0;
}

tcpcb_t *tcp_connect(net_device_t *dev, uint32_t dst_ip, uint16_t dst_port) {
    uint16_t local_port = port_alloc(dev->ip, dst_ip, dst_port);
    if (local_port==0)
        return NULL;

    tcpcb_t *tcb = tcb_alloc(dev, dev->ip, dst_ip, local_port, dst_port);
    if (tcb==NULL)
        return NULL;
    acquire_mutex(&tcb->lock);

    tcb->synfin_cnt = 1; // send SYN
    tcb->state = SYN_SENT;

    tcb_insert(tcb);

    tcp_output(tcb, 0);

    release_mutex(&tcb->lock);
    return tcb;
}

void tcp_send(tcpcb_t *tcb, const void *data, size_t len) {
    acquire_mutex(&tcb->lock);

    switch (tcb->state){
        case SYN_SENT:
        case SYN_RECEIVED:
        case ESTABLISHED:
        case CLOSE_WAIT:
            qwrite(&tcb->sndq, data, len);
            tcp_output(tcb, 0);
            break;
        default:
            break; // TODO close
    }

    release_mutex(&tcb->lock);
}

int64_t tcp_recv(tcpcb_t *tcb, void *buf, size_t n) {
    acquire_mutex(&tcb->lock);

    while (qlen(&tcb->rcvq)==0 && tcb->error==0)
        waitq_wait(&tcb->readers, &tcb->lock);

    // if recieve is over, return EOF or error
    if (qlen(&tcb->rcvq)==0 && tcb->error!=0){
        release_mutex(&tcb->lock);
        if (tcb->error==TCP_EOF)
            return 0;
        return -1;
    }

    uint32_t taken = MIN((uint32_t)n, qlen(&tcb->rcvq));

    qpeek(&tcb->rcvq, buf, taken);
    qdiscard(&tcb->rcvq, taken);

    // receiver-side SWS avoidance ; dont do a window update until we have enough free space
    if (taken>=2*tcb->mss)
        tcp_output(tcb, FORCE);

    release_mutex(&tcb->lock);
    return taken;
}

void tcp_close(tcpcb_t *tcb) {
    acquire_mutex(&tcb->lock);

    switch (tcb->state) {
        case SYN_SENT:
            tcb_close(tcb, TCP_EOF);
            break;
        case SYN_RECEIVED:
        case ESTABLISHED:
            tcb->synfin_cnt++; // send a FIN
            tcb->state = FIN_WAIT_1;
            tcp_output(tcb, 0);
            break;
        case CLOSE_WAIT:
            tcb->synfin_cnt++; // send a FIN
            tcb->state = LAST_ACK;
            tcp_output(tcb, 0);
            break;
        default:
            break;
    }

    if (tcb->error==0)
        tcb->error=TCP_EOF;

    qflush(&tcb->rcvq);
    waitq_broadcast(&tcb->readers);
    waitq_broadcast(&tcb->writers);

    release_mutex(&tcb->lock);
}

#define RXT_MS 1000
#define MAX_RETRIES 8

static int backoff(int n){ // exponentially backoff
    return 1<<n;
}

static uint64_t rto(tcpcb_t *tcb) {
    ASSERT(tcb->lock.current_count>0, "rto called without tcb lock held");

    int r=backoff(tcb->t_rxtcount)*RXT_MS;
    if (r<300) r=300; // clamp
    else if (r>64000) r=64000;
    return (uint64_t)r;
}

static void rxt_fired(timer_t *t) {
    tcpcb_t *tcb = container_of(t, tcpcb_t, t_rxt_timer);
    acquire_mutex(&tcb->lock);

    tcb->t_rxtcount++;
    if (tcb->t_rxtcount > MAX_RETRIES){
        // TODO close
        release_mutex(&tcb->lock);
        return;
    }

    tcb->snd_nxt = tcb->snd_una; // rewind
    tcp_output(tcb, 0); // rearms the timer itself

    release_mutex(&tcb->lock);
}

#define MAX_OUTPUT_SEGMENTS 100

void tcp_output(tcpcb_t *tcb, int opts) {
    ASSERT(tcb->lock.current_count>0, "tcp_output called without tcb lock held");

    if (tcb->state==CLOSED || tcb->state==LISTEN)
        return;

    bool force = opts & FORCE;

    for (int i=0; i<MAX_OUTPUT_SEGMENTS; i++) {
        uint8_t flags = ACK;
        uint32_t owed = qlen(&tcb->sndq) + tcb->synfin_cnt;

        switch (tcb->state) {
            case SYN_RECEIVED:
                if (tcb->snd_nxt==tcb->iss) // SYN ACK the SYN we just got
                    flags |= SYN;
                owed = tcb->synfin_cnt; // dont piggyback on SYN-ACK
                break;
            case SYN_SENT:
                flags=0; // no ACK
                if (tcb->snd_nxt==tcb->iss)
                    flags |= SYN;
                owed = tcb->synfin_cnt; // dont piggyback on SYN
                break;
            default:
                break;
        }

        uint32_t inflight = tcb->snd_nxt-tcb->snd_una;
        uint32_t unsent = owed-inflight;

        uint32_t wnd_room=0;
        if (inflight<tcb->snd_wnd) // avoids underflow
            wnd_room = tcb->snd_wnd-inflight;
        uint32_t len = MIN(MIN(wnd_room, unsent), tcb->mss);

        // only the first segment of a call can be forced,
        // after that were just draining the backlog and the normal checks apply again
        bool forced=force;
        force=false;

        // zero-window probe ; if this doesnt go thru then it will get retransmitted by rtx logic
        if (wnd_room==0 && inflight==0 && unsent>0){
            len=1;
            forced=true;
        }

        // if we r in a state where FIN is owed and we sent over everything
        if ((tcb->state==FIN_WAIT_1 || tcb->state==LAST_ACK) && len==unsent)
            flags |= FIN;

        uint32_t queued = qlen(&tcb->sndq);
        uint32_t data_len = 0;
        if (queued>inflight) // we have data that we havent sent yet
            data_len = MIN(len, queued-inflight);

        // sender-side SWS avoidance
        // stuff unacked? stuff to send? silly? room for more data?
        bool withhold = inflight>0 && data_len>0 && data_len<tcb->mss && data_len<(queued-inflight);

        if (!forced && (len==0 || withhold))
            return; // not enough to send

        block_t *b;
        if (data_len>0)
            b = qcopy(&tcb->sndq, data_len, inflight); // get a block of data_len from sndq
        else
            b = block_alloc(0, HDR_TCP); // we are just sending a flag

        if (b==NULL) return;

        //build header
        tcp_header_t *out = (tcp_header_t *)block_push(b, sizeof(tcp_header_t));
        out->src_port = htons(tcb->local_port);
        out->dst_port = htons(tcb->remote_port);
        out->seq = htonl(tcb->snd_nxt);
        out->ack = htonl(tcb->rcv_nxt);
        out->data_offset = 5<<4;
        out->flags = flags;
        out->window = htons(tcb->rcvq.limit - tcb->rcvq.len);
        out->urgent_ptr = 0;
        out->checksum = 0;
        out->checksum = tcp_checksum(tcb->local_ip, tcb->remote_ip, out, block_len(b));

        tcb->snd_nxt += len;
        tcb->snd_max = SEQ_MAX(tcb->snd_max, tcb->snd_nxt);

        // arm rxt if theres unacked data, and we havent already armed rxt
        if (tcb->snd_nxt!=tcb->snd_una && !tcb->t_rxt_timer.armed)
            timer_arm(&tcb->t_rxt_timer, rto(tcb), rxt_fired);

        if (ipv4_send(tcb->dev, tcb->remote_ip, IPV4_PROTO_TCP, b)<0)
            break; // error, stop looping and just try next retransmit

        // only loop again if this segment went out completely 
        if (data_len<tcb->mss)
            break;
    }
}

static void send_rst(net_device_t *dev, uint32_t dst_ip, uint16_t local_port,
                         uint16_t remote_port, const tcp_header_t *seg, uint32_t payload_len) {

    if (seg->flags & RST)
        return; // never RST a RST

    uint32_t seq, ack;
    uint8_t flags;

    flags=RST;
    if (seg->flags & ACK){
        seq = ntohl(seg->ack);
        ack = 0;
    }else{
        seq = 0;
        ack = ntohl(seg->seq)+payload_len;
        if (seg->flags & SYN) ack++;
        if (seg->flags & FIN) ack++;
        flags|=ACK;
    }

    block_t *b = block_alloc(0, HDR_TCP);
    if (b==NULL) return;

    tcp_header_t *out = (tcp_header_t *)block_push(b, sizeof(tcp_header_t));
    out->src_port = htons(local_port);
    out->dst_port = htons(remote_port);

    out->seq = htonl(seq);
    out->ack = htonl(ack);
    out->flags = flags;

    out->data_offset = 5<<4;
    out->window = 0;
    out->urgent_ptr = 0;
    out->checksum = 0;
    out->checksum = tcp_checksum(dev->ip,dst_ip,out,block_len(b));

    ipv4_send(dev, dst_ip, IPV4_PROTO_TCP, b);
}

static void tcp_parse_options(tcpcb_t *tcb, const tcp_header_t *seg, uint32_t hdr_len) {
    const uint8_t *cur = (const uint8_t *)seg + sizeof(tcp_header_t);
    const uint8_t *end = (const uint8_t *)seg + hdr_len;

    while (cur<end){ // loop over all the options
        uint8_t kind=cur[0];
        if (kind==0) break; // end of option list
        if (kind==1){ // NOP, 1 byte, no length field
            cur+=1;
            continue;
        }

        if (cur+1>=end) break; // no room for length byte
        uint8_t len=cur[1];
        if (len<2 || cur+len>end) break; // malformed

        switch (kind){
            case 2: // MSS
                if (len==4){
                    uint16_t mss_raw;
                    memcpy(&mss_raw, cur+2, sizeof(mss_raw)); // unaligned cast+deref is UB in C even though x86 tolerates it in hardware
                    uint16_t peer_mss = ntohs(mss_raw);
                    if (peer_mss)
                        tcb->mss = MIN(tcb->mss,peer_mss); // clamp ours to their mss
                }
                break;
        }

        cur+=len;
    }
}

void tcp_input(net_device_t *dev, block_t *b, uint32_t src_ip) {
    if (block_len(b)<sizeof(tcp_header_t))
        return;
    const tcp_header_t *seg = (const tcp_header_t *)b->data;

    if (tcp_checksum(dev->ip,src_ip,seg,block_len(b))!=0)
        return;

    uint32_t hdr_len = (seg->data_offset>>4)*4;
    if (hdr_len<sizeof(tcp_header_t) || hdr_len>block_len(b))
        return; // they are giving wrong header len

    uint16_t local_port = ntohs(seg->dst_port);
    uint16_t remote_port = ntohs(seg->src_port);

    tcpcb_t *tcb = tcb_lookup(dev->ip, src_ip, local_port, remote_port);
    if (tcb==NULL) { // state = LISTEN
        listener_t *l = listener_lookup(local_port);
        if (l==NULL){ // we arent listening
            send_rst(dev, src_ip, local_port, remote_port, seg, block_len(b)-hdr_len);
            return;
        }

        // An incoming RST should be ignored. Return.
        if (seg->flags & RST)
            return;

        if (seg->flags & ACK){
            send_rst(dev, src_ip, local_port, remote_port, seg, block_len(b)-hdr_len);
            return;
        }

        if (seg->flags & SYN){
            // TODO check security
            // TODO check prc

            // no data on a bare SYN is queued/processed, same as linux

            tcb = tcb_alloc(dev, dev->ip, src_ip, local_port, remote_port);
            if (tcb==NULL)
                return;


            acquire_mutex(&tcb->lock);

            tcb->listener = l;

            uint32_t seq=ntohl(seg->seq);
            tcb->irs = seq;
            tcb->rcv_nxt = seq+1;

            tcb->snd_wnd = ntohs(seg->window);
            tcb->synfin_cnt = 1; // send SYN
            tcb->state = SYN_RECEIVED;

            tcp_parse_options(tcb, seg, hdr_len);

            tcb_insert(tcb);
            tcp_output(tcb,0);

            release_mutex(&tcb->lock);
            return;
        }

        return;

    }else{
        acquire_mutex(&tcb->lock);

        if (tcb->state==SYN_SENT){
            if (seg->flags & ACK){
                uint32_t seg_ack = ntohl(seg->ack);

                // does this ack ack our syn
                if (!(SEQ_LT(tcb->iss, seg_ack) && SEQ_LEQ(seg_ack, tcb->snd_nxt))){ // if not iss < seg_ack <= snd_nxt
                    send_rst(tcb->dev, tcb->remote_ip, tcb->local_port, tcb->remote_port, seg, block_len(b)-hdr_len);
                    goto unlock;
                }
            }

            // If the RST bit is set
            if (seg->flags & RST){
                // If the ACK was acceptable then signal the user "error: connection reset", 
                // drop the segment, enter CLOSED state, delete TCB, and return.
                if (seg->flags & ACK)
                    tcb_close(tcb, ECONNRESET);
                // else just return
                goto unlock;
            }

            
            if (seg->flags & SYN){
                uint32_t seq = ntohl(seg->seq);
                tcb->irs = seq;
                tcb->rcv_nxt = seq+1;

                tcp_parse_options(tcb, seg, hdr_len);
                tcb->snd_wnd = ntohs(seg->window);

                if (seg->flags & ACK){
                    tcb->snd_una = ntohl(seg->ack);
                    tcb->synfin_cnt--; // our SYN just got acked
                    tcb->state = ESTABLISHED;
                    tcp_output(tcb, FORCE); // final ACK of the handshake
                }

                // TODO simultaneous open
            }

            goto unlock;

        }else{
            uint32_t seg_seq = ntohl(seg->seq);

            // First, check sequence number
            if (seg_seq != tcb->rcv_nxt){
                if (!(seg->flags & RST))
                    tcp_output(tcb, FORCE); // duplicate ack, len is 0 by design here
                goto unlock; // we drop out of order stuff for now
            }

            // second, check RST
            // TODO this is wrong slightly
            if (seg->flags & RST){
                tcb_close(tcb, ECONNRESET);
                goto unlock;
            }

            // fourth, check SYN
            // SYN rn means something bad happened
            // TODO challenge ACK
            if (seg->flags & SYN){
                goto unlock;
            }

            // fifth, check the ACK field
            if (!(seg->flags & ACK)){
                goto unlock;
            }

            uint32_t seg_ack = ntohl(seg->ack);

            switch (tcb->state){
                case SYN_RECEIVED:
                    // If SND.UNA =< SEG.ACK =< SND.NXT then enter ESTABLISHED state and continue processing.
                    if (!(SEQ_LEQ(tcb->snd_una, seg_ack) && SEQ_LEQ(seg_ack, tcb->snd_nxt))){
                        send_rst(tcb->dev, tcb->remote_ip, tcb->local_port, tcb->remote_port, seg, block_len(b)-hdr_len);
                        goto unlock;
                    }
                    tcb->state = ESTABLISHED;

                    // add to the accept q
                    listener_t *l = tcb->listener;
                    acquire_mutex(&l->lock);

                    // if it doesnt fit
                    if (l->nqueued<MAX_INCOMING){

                        tcb->accept_next = NULL;
                        if (l->accept_head==NULL){
                            l->accept_head = l->accept_tail = tcb;
                        }else{
                            l->accept_tail->accept_next = tcb;
                            l->accept_tail = tcb;
                        }

                        l->nqueued++;
                        waitq_broadcast(&l->accept_waiters);
                        release_mutex(&l->lock);
                    }else{
                        // backlog full, RST and tear down
                        release_mutex(&l->lock);
                        send_rst(tcb->dev, tcb->remote_ip, tcb->local_port, tcb->remote_port, seg, block_len(b)-hdr_len);
                        tcb_close(tcb, TCP_EOF);
                        goto unlock;
                    }

                    break;
                default:
                    break;
            }

            tcb->snd_wnd = ntohs(seg->window);

            // ack for data we never sent
            if (SEQ_GT(seg_ack, tcb->snd_max)){
                tcp_output(tcb, FORCE);
                goto unlock;
            }

            // only a new ack advances snd_una
            if (SEQ_GT(seg_ack, tcb->snd_una)){
                // advance snd_una, discarding all that got acked
                // if covers more than that the rest was SYN/FIN
                uint32_t acked = seg_ack - tcb->snd_una;
                uint32_t data_acked = MIN(acked, qlen(&tcb->sndq));
                qdiscard(&tcb->sndq, data_acked);

                if (data_acked<acked) // there is a flag
                    tcb->synfin_cnt--;

                tcb->snd_una = seg_ack;

                if (tcb->snd_una==tcb->snd_nxt){ // if theres nothing left to read
                    if (tcb->t_rxt_timer.armed)
                        timer_cancel(&tcb->t_rxt_timer);
                } else if (acked>0){
                    tcb->t_rxtcount = 0; // they acked something we sent/retransmitted
                    timer_arm(&tcb->t_rxt_timer, rto(tcb), rxt_fired); // restart on new data acked
                }
            }

            bool accepts_data = (tcb->state==ESTABLISHED || tcb->state==FIN_WAIT_1 || tcb->state==FIN_WAIT_2);
            uint32_t payload_len = block_len(b)-hdr_len;

            uint32_t written = 0;
            if (accepts_data && payload_len>0){
                block_pull(b, hdr_len);
                written = qwrite(&tcb->rcvq, b->data, payload_len);
                tcb->rcv_nxt += written; // ack what we wrote
            }

            // only consume the FIN once evertyhing is taken
            bool fin_consumed = accepts_data && (seg->flags & FIN) && written==payload_len;
            if (fin_consumed){
                tcb->rcv_nxt+=1;
                tcb->error=TCP_EOF;

                switch (tcb->state){
                    case ESTABLISHED:
                        tcb->state = CLOSE_WAIT;
                        break;
                    case FIN_WAIT_1:
                        // if our own FIN was also acked by this segment, this is simultaneous close
                        tcb->state = (tcb->synfin_cnt==0) ? TIME_WAIT : CLOSING;
                        break;
                    case FIN_WAIT_2:
                        tcb->state = TIME_WAIT;
                        break;
                    default:
                        break;
                }
            }

            // to acknowledge - snd_una/snd_wnd just moved, which can free up room for previously window-limited data

            // force if we wrote anythign or FIN was consumed
            int out_opts=0;
            if (written>0 || fin_consumed){
                out_opts|=FORCE;
                waitq_broadcast(&tcb->readers);
            }
            tcp_output(tcb, out_opts);
        }

unlock:
        release_mutex(&tcb->lock);
        return;
    }
}

void tcp_init(void) {
    pool_init(&tcb_pool, tcbs, sizeof(tcpcb_t), NTCB);
    pool_init(&listener_pool, listeners, sizeof(listener_t), NLISTEN);

    srand((uint32_t)rdtsc());
    tcb_hash_secret=rand32();
}
