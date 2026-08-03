#include "e1000.h"
#include "kernel.h"
#include "drivers/pci.h"
#include "arch/io.h"
#include "arch/isr.h"
#include "arch/pic.h"
#include "mm/dma.h"
#include "mm/vmm.h"
#include "net/netdev.h"
#include "net/eth.h"
#include "sched/task.h"
#include "terminal/terminal.h"
#include "lib/string.h"
#include <stdbool.h>

#define E1000_VENDOR 0x8086
#define E1000_DEV 0x100E
#define E1000_BAR0_SIZE 0x20000 // the register file is 128KB

#define REG_CTRL 		0x0000
#define REG_STATUS		0x0008
#define REG_EEPROM 		0x0014
#define REG_ICR			0x00C0
#define REG_ITR			0x00C4
#define REG_IMASK 		0x00D0
#define REG_IMC			0x00D8
#define REG_RCTRL 		0x0100
#define REG_MTA 		0x5200 // 128-entry multicast filter table
#define REG_RAL0 		0x5400
#define REG_RAH0 		0x5404
#define REG_RXDESCLO  	0x2800
#define REG_RXDESCHI  	0x2804
#define REG_RXDESCLEN 	0x2808
#define REG_RXDESCHEAD 	0x2810
#define REG_RXDESCTAIL 	0x2818

#define REG_TCTRL 		0x0400
#define REG_TXDESCLO  	0x3800
#define REG_TXDESCHI  	0x3804
#define REG_TXDESCLEN 	0x3808
#define REG_TXDESCHEAD 	0x3810
#define REG_TXDESCTAIL 	0x3818
#define REG_TIPG 	0x0410



#define RCTRL_EN 	0x00000002
#define RCTRL_SBP 	0x00000004
#define RCTRL_UPE 	0x00000008
#define RCTRL_MPE 	0x00000010
#define RCTRL_BAM 	(1<<15) // accept broadcast, needed for arp
#define RCTRL_SECRC 	(1<<26) // strip the ethernet crc, so lengths are payload only
// no bsize bits set below = 2048, matching BLOCK_SIZE exactly

#define RXD_STAT_DD 	0x01 // card has written a frame into this descriptor

#define RXD_ERR_CE 	0x01 // crc error
#define RXD_ERR_SE 	0x02 // symbol error
#define RXD_ERR_SEQ 	0x04 // sequence error
#define RXD_ERR_CXE 	0x10 // carrier extension error
#define RXD_ERR_TCPE 	0x20 // tcp/udp checksum error, not a framing problem
#define RXD_ERR_IPE 	0x40 // ip checksum error, not a framing problem
#define RXD_ERR_RXE 	0x80 // rx data error

// frame corruption, we drop on this
#define RXD_ERR_MASK (RXD_ERR_CE | RXD_ERR_SE | RXD_ERR_SEQ | RXD_ERR_CXE | RXD_ERR_RXE)

#define IMS_LSC 	(1<<2) // link status change
#define IMS_RXO 	(1<<6) // receiver overrun, ring wasnt being drained fast enough
#define IMS_RXT0 	(1<<7) // a frame arrived

// linux's e1000_set_itr "lowest_latency" mode
// ITR units are 256ns
#define ITR_LOWEST_LATENCY 70000 // target interrupts/sec
#define ITR_VALUE (1000000000 / (ITR_LOWEST_LATENCY * 256))

#define ECTRL_FD   	0x01//FULL DUPLEX
#define ECTRL_LRST 	(1<<3)//link reset, must be 0
#define ECTRL_ASDE 	0x20//auto speed enable
#define ECTRL_SLU  	0x40//set link up
#define ECTRL_ILOS 	(1<<7)//invert loss of signal, must be 0
#define ECTRL_VME 	(1<<30)//vlan tagging, must be 0
#define ECTRL_PHYRST 	(1<<31)//phy reset, must be 0
#define ECTRL_RST 	(1<<26)//self clears when the reset finishes

#define EERD_START 	(1<<0)
#define EERD_DONE 	(1<<4)
#define EERD_ADDR_SHIFT 8
#define EERD_DATA_SHIFT 16

#define RAH_AV 		(1<<31) // address valid, turns the unicast filter entry on

#define TCTL_EN 	(1<<1)
#define TCTL_PSP 	(1<<3) // pad short packets
#define TCTL_CT_SHIFT 	4
#define TCTL_CT 	(15 << TCTL_CT_SHIFT) // collision threshold, datasheet recommended value
#define TCTL_COLD_SHIFT 12
#define TCTL_COLD 	(0x40 << TCTL_COLD_SHIFT) // collision distance, full duplex value
#define TCTL_RTLC 	(1<<24) // retransmit on late collision, no-op in full duplex but standard

#define TIPG_IPGR1_SHIFT 10
#define TIPG_IPGR2_SHIFT 20

#define TXD_STAT_DD 	0x01
#define TXD_CMD_EOP 	0x01
#define TXD_CMD_IFCS 	0x02
#define TXD_CMD_RS 	0x08


typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

#define RX_DESC_N 64

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

#define TX_DESC_N 256

static uint32_t initial_bar;
static uint8_t irq_number;
static volatile uint8_t *mmio_addr;
static net_device_t e1000_dev;
static uint8_t mac[6];

static volatile e1000_rx_desc_t *rx;
static block_t *rx_virt[RX_DESC_N];
static uint32_t rx_index;

static volatile e1000_tx_desc_t *tx;
static uint32_t tx_index; // next slot to hand a block to
static block_t *tx_virt[TX_DESC_N];

// q for the drainer to handle
#define RXQ_N 128
static block_t *rxq[RXQ_N];
static unsigned rxq_in, rxq_out; // add (irq) / read (drainer)
static thread_control_block_t *drainer_tcb;

static void e1000_drainer(void);

static void write_command(uint16_t addr, uint32_t val) {
    *((volatile uint32_t *)(mmio_addr+addr)) = val;
}

static uint32_t read_command(uint16_t addr) {
    return *((volatile uint32_t *)(mmio_addr+addr));
}

// resets the card and brings the link up. returns false if RST never self-cleared.
static bool e1000_reset(void) {
    write_command(REG_IMC, 0xFFFFFFFF); // nothing is registered to handle interrupts yet

    write_command(REG_CTRL, read_command(REG_CTRL) | ECTRL_RST);
    (void)read_command(REG_STATUS); // force the write above to have landed before we poll RST

    int i;
    for (i=0; i<100000; i++) { // spin till its done
        if (!(read_command(REG_CTRL) & ECTRL_RST)) break;
    }
    if (i==100000)
        return false;

    // reset can re-enable some interrupt sources, so mask again and ack whats pending
    write_command(REG_IMC, 0xFFFFFFFF);
    (void)read_command(REG_ICR);

    uint32_t ctrl = read_command(REG_CTRL);
    ctrl |= ECTRL_SLU;
    ctrl &= ~(ECTRL_LRST | ECTRL_ILOS | ECTRL_VME | ECTRL_PHYRST);
    write_command(REG_CTRL, ctrl);

    return true;
}

// reset leaves the multicast filter in an unspecified state
static void e1000_clear_mta(void) {
    for (int i=0; i<128; i++)
        write_command(REG_MTA + i*4, 0);
}

// reads one 16-bit word out of the eeprom. returns 0 on timeout.
static uint16_t eeprom_read(uint8_t addr) {
    write_command(REG_EEPROM, ((uint32_t)addr << EERD_ADDR_SHIFT) | EERD_START);

    for (int i=0; i<100000; i++) {
        uint32_t val = read_command(REG_EEPROM);
        if (val & EERD_DONE) return (uint16_t)(val >> EERD_DATA_SHIFT);
    }

    kprintf("e1000: eeprom read of word %d timed out\n", addr);
    return 0;
}

// sets up the descriptor ring and turns receiving on. every descriptor keeps
// the same block forever right now (copy-first, no swap yet)
static void init_rx(void) {
    dma_buf_t ring = dma_alloc(1); // RX_DESC_N*16 bytes fits in one page
    ASSERT(ring.virt != NULL);
    rx = (volatile e1000_rx_desc_t *)ring.virt;

    for (int i=0; i<RX_DESC_N; i++) {
        block_t *b = block_alloc(BLOCK_SIZE, 0);
        ASSERT(b != NULL); // boot time, ram is not a constraint
        rx_virt[i] = b;
        rx[i].addr = KPHYS(b->data);
        rx[i].status = 0;
    }
    rx_index = 0;

    write_command(REG_RXDESCLO, (uint32_t)ring.phys); // physical address to start of the ring
    write_command(REG_RXDESCHI, (uint32_t)(ring.phys >> 32));
    write_command(REG_RXDESCLEN, RX_DESC_N * sizeof(e1000_rx_desc_t)); // ring size
    write_command(REG_RXDESCHEAD, 0); // starting at 0
    write_command(REG_RXDESCTAIL, RX_DESC_N-1); // ending at end

    write_command(REG_RCTRL, RCTRL_EN | RCTRL_BAM | RCTRL_SECRC);

    write_command(REG_IMASK, IMS_RXT0 | IMS_RXO); // interrupts on: frame arrived successfuly, overrun
    write_command(REG_ITR, ITR_VALUE); // cap interrupt rate so bursts drain in one batch
}

// sets up the descriptor ring and turns transmitting on. doesnt touch how
// blocks get attached to a slot or reclaimed - thats send_packet, not this.
static void init_tx(void) {
    dma_buf_t ring = dma_alloc(1); // TX_DESC_N*16 bytes fits in one page
    ASSERT(ring.virt != NULL);
    tx = (volatile e1000_tx_desc_t *)ring.virt;

    for (int i=0; i<TX_DESC_N; i++)
        tx[i].status = TXD_STAT_DD; // looks already-sent, so reusing slot 0 doesnt wait on nothing
    tx_index = 0;

    write_command(REG_TXDESCLO, (uint32_t)ring.phys);
    write_command(REG_TXDESCHI, (uint32_t)(ring.phys >> 32));
    write_command(REG_TXDESCLEN, TX_DESC_N * sizeof(e1000_tx_desc_t));
    write_command(REG_TXDESCHEAD, 0);
    write_command(REG_TXDESCTAIL, 0);

    // recommended inter-packet gap timings for full duplex
    write_command(REG_TIPG, 10 | (8 << TIPG_IPGR1_SHIFT) | (6 << TIPG_IPGR2_SHIFT));

    write_command(REG_TCTRL, TCTL_EN | TCTL_PSP | TCTL_CT | TCTL_COLD | TCTL_RTLC);
}

// attaches b directly to the next slot, no copy. reclaims whatever was there
// before lazily, on the next send that lands on the same slot. never spins:
// if the slot isnt free yet, drops and returns -1 rather than wait.
// takes ownership of b either way, same contract as every other dev->send.
static int send_packet(net_device_t *dev, block_t *b) {
    (void)dev; // only one card right now

    lock_stuff();

    if (!(tx[tx_index].status & TXD_STAT_DD)) {
        unlock_stuff();
        block_free(b);
        return -1;
    }

    if (tx_virt[tx_index])
        block_free(tx_virt[tx_index]);

    tx[tx_index].addr   = KPHYS(b->data);
    tx[tx_index].length = block_len(b);
    tx[tx_index].status = 0; // clear DD ourselves; hardware sets it again on completion
    tx[tx_index].cmd    = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;

    tx_virt[tx_index] = b;

    tx_index = (tx_index + 1) % TX_DESC_N;
    write_command(REG_TXDESCTAIL, tx_index);

    unlock_stuff();
    return 0;
}

void e1000_init(pci_device_t *dev) {
    if (!dev) {
        kprintf("e1000 not found\n");
        return;
    }

    initial_bar = dev->bar[0]; // physical address
    irq_number = dev->irq_line;

    pci_enable_bus_mastering(dev);

    mmio_addr = vmm_map_mmio(initial_bar, E1000_BAR0_SIZE);

    if (!e1000_reset()) {
        kprintf("e1000: reset never completed\n");
        return;
    }

    e1000_clear_mta();
    init_rx();
    init_tx();

    // words 0..2 of the eeprom hold the mac, low byte first within each word
    for (int i=0; i<3; i++) {
        uint16_t word = eeprom_read(i);
        mac[i*2]   = (uint8_t)(word & 0xFF);
        mac[i*2+1] = (uint8_t)(word >> 8);
    }

    // the filter is reset by e1000_reset, so put our own address back in
    uint32_t ral, rah;
    memcpy(&ral, &mac[0], 4);
    memcpy(&rah, &mac[4], 2);
    memset((uint8_t *)&rah + 2, 0, 2);
    rah |= RAH_AV;
    write_command(REG_RAL0, ral);
    write_command(REG_RAH0, rah);

    memcpy(e1000_dev.mac, mac, 6);
    e1000_dev.ip      = 0x0A00020F; // 10.0.2.15, qemu's fixed guest ip
    e1000_dev.netmask = 0xFFFFFF00; // 255.255.255.0
    e1000_dev.gateway = 0x0A000202; // 10.0.2.2, qemu's router (= the host)
    e1000_dev.send    = send_packet;

    irq_register(32 + dev->irq_line, irq_handler);
    IRQ_clear_mask(dev->irq_line);

    drainer_tcb = task_create_prio(e1000_drainer, PRIO_HIGH);

    debugf("e1000: reset ok, status %x, mac %x:%x:%x:%x:%x:%x\n", read_command(REG_STATUS),
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

net_device_t *e1000_netdev(void) {
    return &e1000_dev;
}

// pulls blocks off rxq and runs the protocol stack. a real task, so it can
// take as long as it needs (arp resolution, etc) without the ring caring
static void e1000_drainer(void) {
    for (;;) {
        lock_stuff();
        if (rxq_in==rxq_out)
            block_task(TCB_BLOCKED);
        unlock_stuff();

        block_t *b = rxq[rxq_out%RXQ_N];
        rxq_out++;
        eth_process(&e1000_dev, b);
    }
}

// called from the irq once it has pushed a copied frame onto rxq
static void e1000_drainer_kick(void) {
    if (drainer_tcb)
        unblock_task_from_irq(drainer_tcb);
}

void irq_handler(void *ctx) {
    (void)ctx;

    uint32_t status = read_command(REG_ICR);

    // if something arrived successfully or was dropped because no space
    if (status & (IMS_RXT0 | IMS_RXO)) { 
        bool any = false; // did we free at least one space
        bool pushed = false; // did we hand at least one block to drainer

        while (rx[rx_index].status & RXD_STAT_DD) {
            uint32_t i = rx_index;
            any = true;

            if (!(rx[i].errors & RXD_ERR_MASK)) {
                block_t *b = block_alloc(rx[i].length, 0);
                if (b) {
                    memcpy(b->data, rx_virt[i]->data, rx[i].length);
                    block_put(b, rx[i].length);
                    if (rxq_in-rxq_out < RXQ_N) {
                        rxq[rxq_in%RXQ_N] = b;
                        rxq_in++;
                        pushed = true;
                    } else {
                        block_free(b); // rxq full, drop
                    }
                } // pool empty, drop
            }

            rx[i].status = 0;
            rx_index = (rx_index+1) % RX_DESC_N;
        }

        // one write for the whole batch, not one per descriptor
        if (any)
            write_command(REG_RXDESCTAIL, (rx_index+RX_DESC_N-1) % RX_DESC_N);
        if (pushed)
            e1000_drainer_kick();
    }

    PIC_sendEOI(irq_number);
}

