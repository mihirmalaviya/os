#pragma once
#include "net/netdev.h"

void loopback_init(void);
netdev_t *loopback_netdev_get(void);
