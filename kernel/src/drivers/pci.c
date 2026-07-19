#include "drivers/pci.h"
#include "arch/io.h"
#include "terminal/terminal.h"

pci_device_t pci_devices[PCI_MAX_DEVICES];
int pci_device_count = 0;

typedef struct __attribute__((packed)) {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
    uint32_t bar[6];
    uint32_t cardbus_cis;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t expansion_rom_base;
    uint8_t  capabilities_ptr;
    uint8_t  reserved[7];
    uint8_t  irq_line;
    uint8_t  irq_pin;
    uint8_t  min_grant;
    uint8_t  max_latency;
} pci_header_t;

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) | (offset & 0xFC) | 0x80000000;

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)device << 11) |
        ((uint32_t)function << 8) | (offset & 0xFC) | 0x80000000;

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t get_vendor_id(uint8_t bus, uint8_t device, uint8_t function) {
    return pci_config_read(bus, device, function, 0x00) & 0xFFFF;
}

static void read_header(uint8_t bus, uint8_t device, uint8_t function, pci_header_t *hdr) {
    uint32_t *raw = (uint32_t *)hdr;
    for (size_t i = 0; i < sizeof(*hdr) / 4; i++) {
        raw[i] = pci_config_read(bus, device, function, i * 4);
    }
}

static void check_function(uint8_t bus, uint8_t device, uint8_t function) {
    if (pci_device_count >= PCI_MAX_DEVICES) return; // table full, TODO dynamic

    pci_header_t hdr;
    read_header(bus, device, function, &hdr);

    pci_device_t *dev = &pci_devices[pci_device_count++];
    dev->bus = bus;
    dev->device = device;
    dev->function = function;

    dev->vendor_id   = hdr.vendor_id;
    dev->device_id   = hdr.device_id;
    dev->class_code  = hdr.class_code;
    dev->subclass    = hdr.subclass;
    dev->prog_if     = hdr.prog_if;
    dev->header_type = hdr.header_type;
    dev->irq_line    = hdr.irq_line;

    int bar_count = ((hdr.header_type & 0x7F) == 0) ? 6 : 2;
    for (int i = 0; i < bar_count; i++) {
        uint32_t bar = hdr.bar[i];
        if (bar == 0) {
            dev->bar[i] = 0;
            dev->bar_type[i] = PCI_BAR_NONE;
        } else if (bar & 0x1) { // if bit 0 is set, its I/O space bar
            dev->bar[i] = bar & ~0x3;
            dev->bar_type[i] = PCI_BAR_PIO;
        } else {
            dev->bar[i] = bar & ~0xF;
            dev->bar_type[i] = PCI_BAR_MMIO;
        }
    }
}

static void check_device(uint8_t bus, uint8_t device) {
    uint8_t function = 0;

    uint16_t vendorID = get_vendor_id(bus, device, function);
    if (vendorID == 0xFFFF) return; // device doesnt exist

    check_function(bus, device, function);

    pci_header_t hdr;
    read_header(bus, device, function, &hdr);
    if ((hdr.header_type & 0x80) != 0) {
        // multi-function device, check remaining functions
        for (function = 1; function < 8; function++) {
            if (get_vendor_id(bus, device, function) != 0xFFFF) {
                check_function(bus, device, function);
            }
        }
    }
}

void pci_scan(void) {
    pci_device_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            check_device((uint8_t)bus, device);
        }
    }
}

pci_device_t *pci_find_device(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code && pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

void pci_enable_bus_mastering(pci_device_t *dev) {
    uint32_t cmd = pci_config_read(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1 << 2); // bus master
    cmd |= (1 << 0); // io space
    pci_config_write(dev->bus, dev->device, dev->function, 0x04, cmd);
}

void pci_print_devices(void) {
    for (int i = 0; i < pci_device_count; i++) {
        pci_device_t *dev = &pci_devices[i];

        if (dev->vendor_id == 0x10EC && dev->device_id == 0x8139) {
            kprintf("rtl8139 FOUND\n");
        }

        kprintf("%x:%x.%x vendor=%x device=%x class=%x subclass=%x irq=%d\n",
            dev->bus, dev->device, dev->function,
            dev->vendor_id, dev->device_id,
            dev->class_code, dev->subclass, dev->irq_line);

        for (int b = 0; b < 6; b++) {
            if (dev->bar_type[b] == PCI_BAR_NONE) continue;
            kprintf("  BAR%d: %x (%s)\n", b, dev->bar[b],
                dev->bar_type[b] == PCI_BAR_PIO ? "PIO" : "MMIO");
        }
        // kprintf("\n");
    }
}
