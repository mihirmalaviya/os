#pragma once
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_CLASS_NETWORK 0x02

typedef enum {
    PCI_BAR_NONE = 0,
    PCI_BAR_PIO  = 1,
    PCI_BAR_MMIO = 2,
} pci_bar_type_t;

typedef struct {
    uint8_t bus, device, function;

    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, header_type;

    uint32_t bar[6];
    pci_bar_type_t bar_type[6];

    uint8_t irq_line;
} pci_device_t;

#define PCI_MAX_DEVICES 32
extern pci_device_t pci_devices[PCI_MAX_DEVICES];
extern int pci_device_count;

uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

// scans every bus/device/function and records any device found
void pci_scan(void);

// returns the first recorded device matching class/subclass, or NULL
pci_device_t *pci_find_device(uint8_t class_code, uint8_t subclass);

void pci_print_devices(void);
