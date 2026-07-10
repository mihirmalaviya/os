#include "fs/echfs.h"
#include "drivers/ata.h"
#include "kernel.h"
#include <string.h>

#define ECHFS_SIGNATURE "_ECH_FS_"
#define ECHFS_ALLOC_TABLE_START 16

#define ECHFS_DIR_ENTRY_SIZE 256

#define ALLOC_FREE      0x0000000000000000ULL
#define ALLOC_RESERVED  0xFFFFFFFFFFFFFFF0ULL
#define ALLOC_END_CHAIN 0xFFFFFFFFFFFFFFFFULL

#define DIRID_END      0x0000000000000000ULL
#define DIRID_DELETED  0xFFFFFFFFFFFFFFFEULL
#define DIRID_ROOT     0xFFFFFFFFFFFFFFFFULL

#define OBJ_TYPE_FILE 0
#define OBJ_TYPE_DIR  1

// block 0
typedef struct {
    uint8_t  jump[4];
    uint8_t  signature[8];      // "_ECH_FS_"
    uint64_t total_blocks;
    uint64_t dir_len_blocks;
    uint64_t block_size;
    uint8_t  reserved[4];
    uint8_t  uuid[16];
} __attribute__((packed)) echfs_identity_t;

// 256 bytes, repeated in the main directory region
typedef struct {
    uint64_t dirid;
    uint8_t  obj_type;
    uint8_t  name[201];
    uint64_t atime;
    uint64_t mtime;
    uint16_t perms;
    uint16_t owner;
    uint16_t group;
    uint64_t ctime;
    uint64_t start_or_id;
    uint64_t size;
} __attribute__((packed)) echfs_dirent_t;

_Static_assert(sizeof(echfs_dirent_t) == ECHFS_DIR_ENTRY_SIZE, "echfs_dirent_t must be 256 bytes");

// location of a directory entry on disk, for in-place updates
typedef struct {
    uint64_t block;
    uint32_t index;
} dir_loc_t;

// runtime state, filled in by echfs_mount()
static uint8_t  fs_drive;
static uint64_t fs_block_size;
static uint32_t fs_sectors_per_block;
static uint64_t fs_total_blocks;
static uint64_t fs_alloc_table_blocks;
static uint64_t fs_dir_start_block;
static uint64_t fs_dir_len_blocks;
static uint64_t fs_next_dirid;

void echfs_mount(uint8_t drive) {
    uint16_t sector0[256]; // 512 bytes

    ata_read_sectors(drive, 0, 1, sector0);

    echfs_identity_t *ident = (echfs_identity_t *)sector0;

    if (memcmp(ident->signature, ECHFS_SIGNATURE, 8) != 0) {
        panic("echfs_mount: not an echfs volume");
    }

    fs_drive = drive;
    fs_block_size = ident->block_size;
    fs_sectors_per_block = (uint32_t)(fs_block_size / 512);
    fs_total_blocks = ident->total_blocks;
    fs_dir_len_blocks = ident->dir_len_blocks;

    fs_alloc_table_blocks = ((fs_total_blocks * 8) + fs_block_size - 1) / fs_block_size;
    fs_dir_start_block = ECHFS_ALLOC_TABLE_START + fs_alloc_table_blocks;

    fs_next_dirid = 1;
}
