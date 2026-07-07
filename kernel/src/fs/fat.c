#include "fs/fat.h"
#include "drivers/ata.h"
#include "kernel.h"
#include "terminal/terminal.h"
#include "mm/heap.h"
#include <memory.h>

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_LFN       0x0F

// bpb
typedef struct {
    uint8_t  bootjmp[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  table_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t table_size_16;
    uint16_t sectors_per_track;
    uint16_t head_side_count;
    uint32_t hidden_sector_count;
    uint32_t total_sectors_32;

    // FAT12/16 extended boot record
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fat_type_label[8];
} __attribute__((packed)) fat16_bpb_t;

typedef struct {
    uint8_t   drive;
    uint16_t  bytes_per_sector;
    uint8_t   sectors_per_cluster;
    uint32_t  first_fat_sector;
    uint32_t  first_root_dir_sector;
    uint32_t  root_dir_sectors;
    uint32_t  first_data_sector;
} fat16_fs_t;

// standard 8.3 directory entry
typedef struct {
    uint8_t  name[11];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; // always 0 for FAT16
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat16_dirent_t;

static fat16_fs_t fs;

void fat_init(uint8_t drive) {
    uint16_t boot_sector[256]; // 512 bytes

    ata_read_sectors(drive, 0, 1, boot_sector);

    fat16_bpb_t *bpb = (fat16_bpb_t *)boot_sector;

    uint32_t total_sectors = (bpb->total_sectors_16 == 0)
        ? bpb->total_sectors_32
        : bpb->total_sectors_16;

    uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1))
        / bpb->bytes_per_sector;

    uint32_t first_fat_sector = bpb->reserved_sector_count;
    uint32_t first_root_dir_sector = first_fat_sector + (bpb->table_count * bpb->table_size_16);
    uint32_t first_data_sector = first_root_dir_sector + root_dir_sectors;

    uint32_t data_sectors = total_sectors - first_data_sector;
    uint32_t total_clusters = data_sectors / bpb->sectors_per_cluster;

    if (total_clusters < 4085 || total_clusters >= 65525) {
        panic("fat_init: not a FAT16 volume");
    }
		
    fs.drive = drive;
    fs.bytes_per_sector = bpb->bytes_per_sector;
    fs.sectors_per_cluster = bpb->sectors_per_cluster;
    fs.first_fat_sector = first_fat_sector;
    fs.first_root_dir_sector = first_root_dir_sector;
    fs.root_dir_sectors = root_dir_sectors;
    fs.first_data_sector = first_data_sector;

    // kprintf("fat_init: drive=%d\nbytes_per_sector=%d\nsectors_per_cluster=%d\n"
    //         "first_fat_sector=%d\nfirst_root_dir_sector=%d\nroot_dir_sectors=%d\n"
    //         "first_data_sector=%d\ntotal_clusters=%d\n",
    //         fs.drive, fs.bytes_per_sector, fs.sectors_per_cluster,
    //         fs.first_fat_sector, fs.first_root_dir_sector, fs.root_dir_sectors,
    //         fs.first_data_sector, total_clusters);
}

uint16_t fat_next_cluster(uint16_t cluster) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs.first_fat_sector + (fat_offset / fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs.bytes_per_sector;

    uint16_t buffer[256]; // one sector
    ata_read_sectors(fs.drive, fat_sector, 1, buffer);

    uint8_t *bytes = (uint8_t *)buffer;
    return bytes[entry_offset] | (bytes[entry_offset + 1] << 8);
}

uint8_t *fat_read_file(uint16_t first_cluster, uint32_t file_size) {
    uint32_t cluster_size = fs.sectors_per_cluster * fs.bytes_per_sector;
    uint32_t alloc_size = ((file_size + cluster_size - 1) / cluster_size) * cluster_size;

    uint8_t *buffer = (uint8_t *)kmalloc(alloc_size);
    uint32_t offset = 0;
    uint16_t cluster = first_cluster;

    while (cluster < 0xFFF8) {
        uint32_t sector = (cluster - 2) * fs.sectors_per_cluster + fs.first_data_sector;

        ata_read_sectors(fs.drive, sector, fs.sectors_per_cluster, (uint16_t *)(buffer + offset));

        offset += cluster_size;
        cluster = fat_next_cluster(cluster);
    }

    return buffer;
}

// name must be the raw 11-byte 8.3 name (e.g. "BIGFILE TXT", space-padded, no dot)
int fat_find_file(const char *name, uint16_t *cluster, uint32_t *file_size) {
    uint32_t size = fs.root_dir_sectors * fs.bytes_per_sector;
    uint8_t *buffer = (uint8_t *)kmalloc(size);

    ata_read_sectors(fs.drive, fs.first_root_dir_sector, fs.root_dir_sectors, (uint16_t *)buffer);

    fat16_dirent_t *entries = (fat16_dirent_t *)buffer;
    uint32_t entry_count = size / 32;

    for (uint32_t i = 0; i < entry_count; i++) {
        fat16_dirent_t *entry = &entries[i];

        if (entry->name[0] == 0x00) break;
        if (entry->name[0] == 0xE5) continue;
        if (entry->attributes == FAT_ATTR_LFN) continue;

        if (memcmp(entry->name, name, 11) == 0) {
            *cluster = entry->first_cluster_low;
            *file_size = entry->file_size;
            return 1;
        }
    }

    return 0;
}

void fat_read_root_dir(void) {
    uint32_t size = fs.root_dir_sectors * fs.bytes_per_sector;
    uint8_t *buffer = (uint8_t *)kmalloc(size);

    ata_read_sectors(fs.drive, fs.first_root_dir_sector, fs.root_dir_sectors, (uint16_t *)buffer);

    fat16_dirent_t *entries = (fat16_dirent_t *)buffer;
    uint32_t entry_count = size / 32;

    for (uint32_t i = 0; i < entry_count; i++) {
        fat16_dirent_t *entry = &entries[i];

        if (entry->name[0] == 0x00) break;
        if (entry->name[0] == 0xE5) continue;
        if (entry->attributes == FAT_ATTR_LFN) continue;

        char name[12];
        memcpy(name, entry->name, 11);
        name[11] = '\0';

        kprintf("%s cluster=%d size=%d dir=%d\n",
                name, entry->first_cluster_low, entry->file_size,
                (entry->attributes & FAT_ATTR_DIRECTORY) != 0);
    }
}
