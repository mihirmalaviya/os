#pragma once
#include <stdint.h>
#include "net/netdev.h"

void rtl8139_init(void);
netdev_t *rtl8139_netdev(void);
void rtl8139_irq_handler(void *ctx);
