#pragma once
#include <stdint.h>
#include "net/netdev.h"
#include "drivers/pci.h"

void rtl8139_init(pci_device_t *dev);
net_device_t *rtl8139_netdev(void);
void rtl8139_irq_handler(void *ctx);
