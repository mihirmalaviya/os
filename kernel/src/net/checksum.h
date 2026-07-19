#pragma once
#include <stddef.h>
#include <stdint.h>

// RFC 1071
uint16_t checksum(const void *data, size_t len);
uint32_t checksum_partial(uint32_t sum, const void *data, size_t len);
uint16_t checksum_fold(uint32_t sum);
