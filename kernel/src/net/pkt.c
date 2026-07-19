#include "net/pkt.h"

void pbuf_init(pbuf_t *p, uint8_t *buf, size_t sz, size_t hr) {
    p->head = buf;
    p->data = buf+hr;
    p->end = buf+sz;
}

size_t pbuf_len(const pbuf_t *p) {
    return (size_t)(p->end - p->data);
}

uint8_t *pbuf_add_header(pbuf_t *p, size_t n) {
    p->data -= n;
    return p->data;
}
