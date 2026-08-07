#pragma once
#include <stdint.h>
#include <stdbool.h>

// headroom
#define HDR_LINK 14 // ethernet
#define HDR_IP   34 // ethernet(14) + ipv4(20)
#define HDR_UDP  42 // ethernet(14) + ipv4(20) + udp(8)
#define HDR_TCP  54 // ethernet(14) + ipv4(20) + tcp(20)

#define BLOCK_SIZE 2048 // a bit big but fits in a page well
                        // TODO have a smaller alternative so an ACK doesnt take up 2kb
#define BLOCK_NUM  32768 // 2^15

typedef struct block {
    struct block *next;
    uint8_t *head; // start of allocation
    uint8_t *data; // start of payload
    uint8_t *tail; // end of payload
    uint8_t *end; // end of allocation
    bool in_use; // debug only, so a double free trips at the source
} block_t;

void block_init(void);

block_t *block_alloc(uint32_t size, uint32_t headroom); // NULL when the pool is empty
void block_free(block_t *b);

uint32_t block_len(const block_t *b); // bytes of data
uint32_t block_room(const block_t *b); // spare room at the tail

uint8_t *block_push(block_t *b, uint32_t n); // prepend n bytes, returns the new data
uint8_t *block_pull(block_t *b, uint32_t n); // strip n bytes off the front
uint8_t *block_put(block_t *b, uint32_t n); // extend the tail, returns the old tail
void block_reserve(block_t *b, uint32_t n); // set headroom, block must be empty
void block_trim(block_t *b, uint32_t n); // shrink the tail to n bytes of payload

uint32_t block_nfree(void);
