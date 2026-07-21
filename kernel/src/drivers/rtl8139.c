#include "drivers/rtl8139.h"
#include "drivers/pci.h"
#include "arch/io.h"
#include "arch/isr.h"
#include "arch/pic.h"
// #include "mm/pmm.h"
#include "mm/dma.h"
#include "net/netdev.h"
#include "net/eth.h"
#include "net/pool.h"
#include "sched/task.h"
#include "lib/string.h"
#include "terminal/terminal.h"

#define RTL8139_RX_BUF_PAGES 3 // 8192 + 16 bytes, rounded up to pages
#define RTL8139_TX_SLOTS 4
#define RTL8139_TX_SLOT_SIZE 2048 // frames max ~1.5KB

#define RTL8139_ISR_ROK 0x01
#define RTL8139_ISR_TOK 0x04

#define RTL8139_TSD_TOK (1 << 15) // card sets this in the slot's TSD when the send is done

static uint16_t io_base;
static uint8_t irq_line;

static net_device_t rtl_netdev;

static dma_buf_t rx_buf;
static int rx_offset; // where we've read up to in the rx ring

static dma_buf_t tx_buf;
static int tx_head;      // next empty slot
static int tx_oldest;    // oldest slot that hasnt finished yet
static int tx_inflight;  // slots that we are waiting on to finish
static SEMAPHORE tx_sem; // counts free slots

#define RXQ_N 64
typedef struct {
    uint8_t *buf; // pool-buffer pointer (not the bytes)
    uint16_t len; // length of the queued frame
} rxq_entry_t;
static rxq_entry_t rxq[RXQ_N];
static unsigned    rxq_in, rxq_out; // write (irq) / read (drainer)
static SEMAPHORE   rx_ready;     // counts frames waiting in the ring

static uint8_t *tx_slot_virt(int slot) {
    return (uint8_t *)tx_buf.virt + slot * RTL8139_TX_SLOT_SIZE;
}

static uint32_t tx_slot_phys(int slot) {
    return (uint32_t)(tx_buf.phys + slot * RTL8139_TX_SLOT_SIZE);
}

static int rtl8139_send(net_device_t *dev, const void *frame, size_t len);

static void rtl8139_reset(uint16_t io_base) {
    outb(io_base + 0x37, 0x10);
    while (inb(io_base + 0x37) & 0x10){} // block until its good
}

// drains the rx mailbox and runs the protocol stack. a real task, so it may
// block (eth_process -> arp reply -> rtl8139_send can wait on tx_sem).
static void rx_drainer(void) {
    for (;;) {
        semaphore_acquire(&rx_ready); // sleeps until the irq enqueues a frame
        uint8_t *buf = rxq[rxq_out % RXQ_N].buf;
        uint16_t len = rxq[rxq_out % RXQ_N].len;
        rxq_out++;
        eth_process(&rtl_netdev, buf, len);
        pool_free(buf);
    }
}

void rtl8139_init(pci_device_t *dev) {
    if (!dev) {
        kprintf("rtl8139 not found");
        return;
    }

    io_base = dev->bar[0];
    irq_line = dev->irq_line;

    pci_enable_bus_mastering(dev);

    outb(io_base + 0x52, 0x0); // turn it on
    rtl8139_reset(io_base);

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
        mac[i] = inb(io_base + i);
    }
    kprintf("rtl8139 mac: %x:%x:%x:%x:%x:%x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    rx_buf = dma_alloc(RTL8139_RX_BUF_PAGES); // give it a buffer to write to
    if (!rx_buf.virt) {
        kprintf("rtl8139: rx buffer alloc failed\n");
        return;
    }
    outl(io_base + 0x30, (uint32_t)rx_buf.phys); // init recieve buffer

    tx_buf = dma_alloc(2); // 8KB = 4 slots x 2KB
    if (!tx_buf.virt) {
        kprintf("rtl8139: tx buffer alloc failed\n");
        return;
    }
    tx_head = 0;
    tx_oldest = 0;
    tx_inflight = 0;
    semaphore_init(&tx_sem, RTL8139_TX_SLOTS);


    outw(io_base + 0x3C, 0x0005); // Sets the TOK and ROK bits high
                                  // ROK = interrupt: a packet just arrived and its in ur buffer
                                  // TOK = interrupt: your packet finished sending

    outl(io_base + 0x44, 0xf | (1 << 7)); // (1 << 7) is the WRAP bit, 0xf is AB+AM+APM+AAP

    outb(io_base + 0x37, 0x0C); // Sets the RE and TE bits high
                                // RE = recieve
                                // TE = transmit


    irq_register(32 + dev->irq_line, rtl8139_irq_handler); // connect it to irq
    IRQ_clear_mask(dev->irq_line);

    // register: fill in the netdev record the stack will use
    memcpy(rtl_netdev.mac, mac, 6);
    rtl_netdev.ip      = 0x0A00020F; // 10.0.2.15, qemu's fixed guest ip
    rtl_netdev.netmask = 0xFFFFFF00; // 255.255.255.0
    rtl_netdev.gateway = 0x0A000202; // 10.0.2.2, qemu's router (= the host)
    rtl_netdev.send    = rtl8139_send;

    // rx pipeline: pool of buffers, mailbox doorbell, and the drainer task
    pool_init();
    semaphore_init(&rx_ready, 0);
    task_create(rx_drainer);
}

net_device_t *rtl8139_netdev(void) {
    return &rtl_netdev;
}

// only reachable through the netdev record; the stack calls dev->send
static int rtl8139_send(net_device_t *dev, const void *frame, size_t len) {
    (void)dev; // only one card, the statics already point at it
    if (len > 1792) return -1; // hardware limit per slot

    semaphore_acquire(&tx_sem); // sleeps here if all 4 slots are in flight

    int slot = tx_head;
    tx_head = (tx_head + 1) % RTL8139_TX_SLOTS;

    memcpy(tx_slot_virt(slot), frame, len);
    // if the packet was below minimum length, add 0s to hide the old garbage data
    if (len < 60) { 
        memset(tx_slot_virt(slot) + len, 0, 60 - len);
        len = 60;
    }

    tx_inflight++;
    outl(io_base + 0x20 + slot * 4, tx_slot_phys(slot)); // TSAD: physical address of frame
    outl(io_base + 0x10 + slot * 4, len);                // TSD: length

    return 0;
}

// the 4 bytes the card writes in front of every received frame
typedef struct {
    uint16_t status;
    uint16_t len;
} __attribute__((packed)) rx_prefix_t;

void rtl8139_irq_handler(void *ctx) {
    (void)ctx;

    uint16_t status = inw(io_base + 0x3E);
    outw(io_base + 0x3E, status); // write back to ack, or it re-fires forever

    // wraps the task-waking so switches are deferred to check_postponed_switch()
    // on isr exit instead of happening inline here
    postpone_switches();

    // packet finished sending
    if (status & RTL8139_ISR_TOK) {
        while (tx_inflight > 0) {
            uint32_t tsd = inl(io_base + 0x10 + tx_oldest * 4); // oldest slot's status

            // if not finished
            if (!(tsd & RTL8139_TSD_TOK))
                break; // still sending, everything after it is too

            tx_oldest = (tx_oldest+1) % RTL8139_TX_SLOTS;
            tx_inflight--;
            semaphore_release_from_irq(&tx_sem);
        }
        // kprintf("RTL8139: Sent Packet\n");
    }

    // Received
    // put it in pool buffer, and wake drainer
    if (status & RTL8139_ISR_ROK) {
        while (!(inb(io_base + 0x37) & 0x01)) { // while leftmost bit is == 0
                                                // 0 = at least one frame is waiting
                                                // 1 = empty
            uint8_t *frame = (uint8_t *)rx_buf.virt + rx_offset;
            rx_prefix_t *prefix = (rx_prefix_t *)frame; // first 2 bytes is status, next 2 bytes is len

            if (prefix->status & 0x01) { // frame arrived intact
                if (prefix->len<4) prefix->len=4;
                uint16_t len = prefix->len-4; // drop the 4-byte status/len prefix
                uint8_t *buf = pool_alloc();
                if (buf!=NULL && len<=POOL_SIZE && (rxq_in-rxq_out)<RXQ_N) {
                    memcpy(buf, frame+4, len);
                    rxq[rxq_in%RXQ_N].buf = buf;
                    rxq[rxq_in%RXQ_N].len = len;
                    rxq_in++;
                    semaphore_release_from_irq(&rx_ready);
                } else if (buf!=NULL) {
                    pool_free(buf); // ring cant fit it, so drop it
                }
            }

            rx_offset = (rx_offset + 4+prefix->len +3)&~3; // prefix+frame, round to next multiple of 4
            rx_offset %= 8192;
            outw(io_base+0x38, rx_offset-16);
        }
        // kprintf("RTL8139: Recieved Packet\n");
    }

    unpostpone_switches();
    PIC_sendEOI(irq_line);
}
