#pragma once
#include <stdint.h>

#define ATA_DRIVE_MASTER 0
#define ATA_DRIVE_SLAVE  1

// reads count 512-byte sectors into buffer (must hold count * 256 uint16_t's)
void ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, void *buffer);

// writes count 512-byte sectors from buffer (must hold count * 256 uint16_t's)
void ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, uint16_t *buffer);
