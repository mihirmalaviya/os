#pragma once
#include <stdint.h>
#include <stddef.h>

int http_get(uint32_t ip, uint16_t port, const char *host, const char *path, char *buf, size_t buflen);
int http_build_get(char *buf, size_t buflen, const char *host, const char *path); // returns request length
