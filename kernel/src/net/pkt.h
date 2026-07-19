#pragma once
#include <stdint.h>
#include <stddef.h>

#define PBUF_HEADROOM 64

typedef struct pbuf {
    uint8_t *head; // start of buf
    uint8_t *data; // start of payload
    uint8_t *end;  // end of buf
} pbuf_t;

void     pbuf_init(pbuf_t *p, uint8_t *buf, size_t size, size_t headroom);
size_t   pbuf_len(const pbuf_t *p);
uint8_t *pbuf_add_header(pbuf_t *p, size_t n);  // prepend n bytes of header
