# mihir's os

A small x86-64 kernel written from scratch in C and assembly, with its own TCP/IP stack and a driver for the Intel e1000 network card. It boots with Limine and runs in QEMU.

## Quickstart

Needs: GNU make, gcc or clang, nasm, xorriso, qemu

```sh
git clone https://github.com/mihirmalaviya/os && cd os
./kernel/get-deps
make run
```

`kernel.log` gets the debug output, and `capture.pcap` gets every packet

## what's in it

**Boot and CPU**
- Limine boot, GDT, IDT, ISRs, 8259 PIC
- PIT timer, TSC calibration for timing in nanoseconds

**Memory**
- Bitmap physical page allocator, including contiguous allocations for DMA
- 4-level paging: map, unmap, and MMIO mappings
- Kernel heap (a bump allocator for now)

**Scheduling**
- Preemptive multitasking with 50 ms time slices and two priority levels
- Sleep, blocking and unblocking, semaphores, mutexes, wait queues
- Idle task, and a cleaner task that reaps finished threads

**Drivers**
- PCI enumeration
- Intel e1000 NIC: MMIO setup, DMA descriptor rings (64 RX, 256 TX), interrupt-driven RX/TX
- ATA disk (PIO), PS/2 keyboard, framebuffer terminal with PSF fonts

**Networking**
- Ethernet, ARP with a cache, IPv4, UDP
- TCP:
  - the full connection state machine
  - retransmission with Jacobson/Karels RTO and exponential backoff
  - slow start and congestion avoidance
  - MSS option, silly-window-syndrome avoidance, Nagle-style coalescing
  - TIME_WAIT
- Randomized ISNs and ephemeral ports, a keyed connection hash, and a bounded accept backlog
- Hashed timer wheel (8192 × 1 ms slots) driving every TCP timer
- BSD-style sockets (`socket`, `bind`, `listen`, `accept`, `connect`, non-blocking connect) and an epoll-style readiness API
- A tiny HTTP GET client
- Zero-copy packet buffers with headroom, like Linux `sk_buff`s

**Filesystems**
- VFS with mount points, a tar filesystem as root, and `/proc`
- FAT and echfs readers (not wired in yet)

**Shell**
- `ls`, `cat`, `tasks`, `lspci`, `clear`
