#include "drivers/ata.h"
#include "arch/io.h"

// master drive
#define ATA_DATA        0x1F0
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7

// Drive / Head Register (I/O base + 6)
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7

// Status Register (I/O base + 7)
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

static void ata_wait_bsy(void) {
    while (inb(ATA_STATUS) & ATA_SR_BSY);
}

static void ata_wait_drq(void) {
    while (!(inb(ATA_STATUS) & ATA_SR_DRQ));
}

// reads at most 255 sectors in a single ATA command (ATA_SECCOUNT is an 8-bit register)
static void ata_read_sectors_chunk(uint8_t drive, uint32_t lba, uint8_t count, uint16_t *buffer) {
    ata_wait_bsy();

    uint8_t drive_select = (drive == ATA_DRIVE_SLAVE) ? 0xF0 : 0xE0;
    outb(ATA_DRIVE_HEAD, drive_select | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, count); // count of sections
    outb(ATA_LBA_LOW,  (uint8_t)(lba));
    outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, ATA_CMD_READ);

    for (int sector = 0; sector < count; sector++) {
        ata_wait_bsy();
        ata_wait_drq();

        for (int i = 0; i < 256; i++) {
            buffer[sector * 256 + i] = inw(ATA_DATA);
        }
    }
}

void ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, void *buf) {
    uint16_t *buffer = (uint16_t *)buf;

    while (count > 0) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;

        ata_read_sectors_chunk(drive, lba, chunk, buffer);

        lba += chunk;
        buffer += chunk * 256;
        count -= chunk;
    }
}

// writes at most 255 sectors in a single ATA command (ATA_SECCOUNT is an 8-bit register)
static void ata_write_sectors_chunk(uint8_t drive, uint32_t lba, uint8_t count, uint16_t *buffer) {
    ata_wait_bsy();

    uint8_t drive_select = (drive == ATA_DRIVE_SLAVE) ? 0xF0 : 0xE0;
    outb(ATA_DRIVE_HEAD, drive_select | ((lba >> 24) & 0x0F));
    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LOW,  (uint8_t)(lba));
    outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    for (int sector = 0; sector < count; sector++) {
        ata_wait_bsy();
        ata_wait_drq();

        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA, buffer[sector * 256 + i]);
        }
    }

    // must flush the drive's write cache, or writes can silently fail later
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ata_wait_bsy();
}

void ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, uint16_t *buffer) {
    while (count > 0) {
        uint8_t chunk = (count > 255) ? 255 : (uint8_t)count;

        ata_write_sectors_chunk(drive, lba, chunk, buffer);

        lba += chunk;
        buffer += chunk * 256;
        count -= chunk;
    }
}
