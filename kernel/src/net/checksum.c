#include "net/checksum.h"

uint32_t checksum_partial(uint32_t sum, const void *data, size_t len) {
    const uint16_t *words = (const uint16_t *)data;

    while (len>1) {
        sum+=*words++;
        len-=2;
    } if (len==1) {
        sum += *(const uint8_t *)words;
    }
    return sum;
}

uint16_t checksum_fold(uint32_t sum) {
    while (sum>>16) {
        sum = (sum&0xFFFF) + (sum>>16);
    }
    return (uint16_t)~sum;
}

uint16_t checksum(const void *data, size_t len) {
    return checksum_fold(checksum_partial(0, data, len));
}
