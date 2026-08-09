#include "net/test/stress_test.h"
#include "net/http.h"
#include "net/tcp.h"
#include "net/epoll.h"
#include "net/socket.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "drivers/e1000.h"
#include "terminal/terminal.h"
#include "sched/task.h"
#include "arch/pit.h"
#include <nanoprintf.h>
#include <stdint.h>
#include <stdbool.h>

#define IP4(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))
#define MIN(a,b) ((a)<(b)?(a):(b))

#define TARGET_IP IP4(90,130,70,73) // speedtest.tele2.net
#define TARGET_PORT 80
#define TARGET_BYTES 1048576 // 1mb

#define NCONN 500 // conns per test

static epoll_t ep;

void stress_http(void) {
    unlock_scheduler();

    epoll_create(&ep);

    uint64_t t_start = now_ms();
    debugf("stresstest: start t=%llums\n", (unsigned long long)t_start);

    uint64_t tx_drops_before = e1000_tx_drops(), rx_drops_before = e1000_rx_drops();

    int started=0;
    for (int i=0;i<NCONN;i++){
        int fd=socket();
        if (fd<0 || connect_nb(fd, TARGET_IP, TARGET_PORT)<0){ // connect to them all
            debugf("stresstest: conn %d: connect failed\n", i);
            continue;
        }

        char req[128];
        int n = http_build_get(req, sizeof req, "speedtest.tele2.net", "/1MB.zip");
        write(fd, req, n);

        tcpcb_t *tcb = sock_tcb(fd);
        epoll_add(&ep, tcb);
        started++;
    }
    debugf("stresstest: %d/%d connections started\n", started, NCONN);
    debugf("stresstest: e1000 tx_drops=%llu rx_drops=%llu during setup\n",
           (unsigned long long)(e1000_tx_drops()-tx_drops_before),
           (unsigned long long)(e1000_rx_drops()-rx_drops_before));

    char buf[1460];
    int done=0, ok=0;
    while (done<started){
        tcpcb_t *tcb = epoll_wait(&ep);

        int64_t r;
        while ((r=tcp_recv_nb(tcb, buf, sizeof buf))>0){} // drain what we get
        if (r==-2)
            continue;
        // if we are here it must be end of connection

        if (r==0 && tcb->bytes_received>=TARGET_BYTES)
            ok++;

        done++;
        debugf("stresstest: %d/%d done, ok=%d, retransmits=%llu\n",
               done, started, ok, (unsigned long long)tcp_retransmit_count());

        if (done%100==0)
            kprintf("%d done for http test, ok=%d retransmits=%llu\n", done, ok, (unsigned long long)tcp_retransmit_count());

        epoll_remove(&ep, tcb);
        tcp_close(tcb);
    }

    uint64_t t_end = now_ms();
    debugf("stresstest: done - total e1000 tx_drops=%llu rx_drops=%llu\n",
           (unsigned long long)e1000_tx_drops(), (unsigned long long)e1000_rx_drops());
    debugf("stresstest: end t=%llums elapsed=%llums\n",
           (unsigned long long)t_end, (unsigned long long)(t_end-t_start));
    for (;;)
        asm ("hlt");
}

#define ECHO_TARGET_BYTES 1000000
#define ECHO_IP IP4(45,79,112,203) // tcpbin.com
#define ECHO_PORT 4242

static epoll_t echo_ep;
static char aaa[1460];

static bool advance_echo(tcpcb_t *tcb) {
    char buf[1460];
    int64_t r;
    while ((r=tcp_recv_nb(tcb, buf, sizeof buf))>0){} // drain the echo

    if (tcb->sock!=NULL && tcb->bytes_sent<ECHO_TARGET_BYTES){
        while (tcb->bytes_sent<ECHO_TARGET_BYTES){
            uint32_t want = MIN((uint32_t)sizeof aaa, ECHO_TARGET_BYTES-(uint32_t)tcb->bytes_sent);
            int64_t w = tcp_send(tcb, aaa, want);
            if (w<=0) break; // sndq full
        }
        if (tcb->bytes_sent>=ECHO_TARGET_BYTES)
            tcp_close(tcb);
    }

    return r!=-2;
}

void stress_echo(void) {
    unlock_scheduler();
    epoll_create(&echo_ep);
    memset(aaa, 'a', sizeof aaa);

    int started=0;
    for (int i=0;i<NCONN;i++){
        int fd=socket();
        if (fd<0 || connect_nb(fd, ECHO_IP, ECHO_PORT)<0){
            debugf("echotest: conn %d: connect failed\n", i);
            continue;
        }

        tcpcb_t *tcb = sock_tcb(fd);
        advance_echo(tcb); // queue the first chunk right away
        epoll_add(&echo_ep, tcb);
        started++;
    }
    debugf("echotest: %d/%d connections started\n", started, NCONN);

    int done=0;
    while (done<started){
        tcpcb_t *tcb = epoll_wait(&echo_ep);
        if (!advance_echo(tcb))
            continue; // still going, wait for the next notification

        done++;
        debugf("echotest: %d/%d done, retransmits=%llu\n",
               done, started, (unsigned long long)tcp_retransmit_count());

        if (done%100==0)
            kprintf("%d done for echo test, retransmits=%llu\n", done, (unsigned long long)tcp_retransmit_count());

        epoll_remove(&echo_ep, tcb);
        if (tcb->sock!=NULL) // not already closed inside advance_echo
            tcp_close(tcb);
    }

    debugf("echotest: done\n");
    for (;;)
        asm ("hlt");
}
