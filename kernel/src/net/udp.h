#pragma once
#include <stdint.h>
#include <stddef.h>
#include "net/netdev.h"

int udp_send(net_device_t *dev, uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const void *payload, size_t len);

void udp_process(const void *buffer, size_t len, uint32_t src_ip);

int udp_bind(uint16_t port);

int udp_recv(uint16_t port, uint32_t *src_ip, uint16_t *src_port, void *buf, size_t buflen);
