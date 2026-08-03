#pragma once
#include <stdint.h>
#include "net/netdev.h"
#include "drivers/pci.h"

void e1000_init(pci_device_t *dev);
net_device_t *e1000_netdev(void);
void irq_handler(void *ctx);
