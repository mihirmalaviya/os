#pragma once
#include <stdint.h>
#include <stddef.h>

// GET path from host at ip:port, write the response body into buf.
// returns bytes written, or -1 on error.
int http_get(uint32_t ip, uint16_t port, const char *host, const char *path,
             char *buf, size_t buflen);
