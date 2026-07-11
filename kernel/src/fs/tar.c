#include "fs/tar.h"
#include "drivers/ata.h"
#include <string.h>
#include <nanoprintf.h>

#define TAR_DRIVE ATA_DRIVE_SLAVE // TODO get this from the mountpoints device

// these are all in ascii octal
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) ustar_header_t;

// _Static_assert(sizeof(ustar_header_t) == 512, "ustar_header_t must be exactly one 512-byte sector");

typedef struct {
    int in_use;
    uint32_t start_lba;
    uint32_t size;
    uint32_t position;
} tar_file_t;

#define TAR_MAX_OPEN_FILES 16
static tar_file_t tar_open_files[TAR_MAX_OPEN_FILES];

typedef struct {
    int in_use;
    uint32_t current_lba; // where readdir should resume scanning from next
    char prefix[VFS_PATH_LENGTH]; // directory path being listed, always ends in '/' (or empty for root)
} tar_dir_t;

#define TAR_MAX_OPEN_DIRS 8
static tar_dir_t tar_open_dirs[TAR_MAX_OPEN_DIRS];

size_t octascii_to_dec(char *number, int size) {
    size_t result = 0;
    char *end = number + size;

    for (char *p = number; p < end; p++) {
        if (*p < '0' || *p > '7') {
            break;
        }
        result = (result * 8) + (*p - '0');
    }
    return result;
}


static const char *strip_dot_slash(const char *name) {
    if (name[0] == '.' && name[1] == '/') {
        return name + 2;
    }
    return name;
}

static int is_zeroed(ustar_header_t *h) {
    unsigned char *p = (unsigned char *)h;
    for (size_t i = 0; i < sizeof(*h); i++) {
        if (p[i] != 0) return 0;
    }
    return 1;
}

static void tar_header_name(ustar_header_t *h, char *out, size_t out_size) {
    const char *name = strip_dot_slash(h->name);
    if (h->prefix[0] != 0) {
        npf_snprintf(out, out_size, "%s%s", h->prefix, name);
    } else {
        strcpy(out, name);
    }
}

static size_t tar_strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

static int tar_has_slash(const char *s) {
    for (size_t i = 0; s[i+1] != '\0'; i++) {
        if (s[i] == '/') return 1;
    }
    return 0;
}

// walks the archive looking for `path`; on success fills *out_lba/*out_size and returns 1
static int tar_file_lookup(const char *path, uint32_t *out_lba, uint32_t *out_size) {
    ustar_header_t header;
    uint32_t lba = 0;
    int zero_counter = 0;

    while (zero_counter < 2) {
        ata_read_sectors(TAR_DRIVE, lba, 1, &header);

        if (is_zeroed(&header)) {
            zero_counter++;
            lba += 1;
            continue;
        }
        zero_counter = 0;

        char full_name[256];
        tar_header_name(&header, full_name, sizeof(full_name));

        uint32_t file_size = octascii_to_dec(header.size, 12);
        uint32_t data_lba = lba + 1;

        if (strcmp(full_name, path) == 0) {
            *out_lba = data_lba;
            *out_size = file_size;
            return 1;
        }

        lba = data_lba + (file_size + 511) / 512;
    }

    return 0; // not found
}

int tar_open(const char *path, int flags) {
    uint32_t found_lba, found_size;
    if (!tar_file_lookup(path, &found_lba, &found_size)) {
        return -1;
    }

    for (int i = 0; i < TAR_MAX_OPEN_FILES; i++) {
        if (!tar_open_files[i].in_use) {
            tar_open_files[i].in_use = 1;
            tar_open_files[i].start_lba = found_lba;
            tar_open_files[i].size = found_size;
            tar_open_files[i].position = 0;
            return i;
        }
    }

    return -1; // table full, TODO dynamic
}

int tar_close(int fs_file_id) {
    if (fs_file_id < 0 || fs_file_id >= TAR_MAX_OPEN_FILES || !tar_open_files[fs_file_id].in_use)
        return -1;

    tar_open_files[fs_file_id].in_use=0;
    return 0;
}

int64_t tar_read(int fs_file_id, void *buf, uint32_t nbyte) {
    if (fs_file_id < 0 || fs_file_id >= TAR_MAX_OPEN_FILES || !tar_open_files[fs_file_id].in_use)
        return -1;

    tar_file_t *f = &tar_open_files[fs_file_id];

    if (f->position >= f->size)
        return 0;

    if (f->position + nbyte > f->size) // if we wanna read too much 
        nbyte = f->size - f->position; // lower it so we arent reading past EOF

    uint32_t bytes_copied = 0;
    uint8_t sector_buf[512];

    while (bytes_copied < nbyte) { // keep reading from sectors until we got it all
        uint32_t byte_offset = f->position + bytes_copied;
        uint32_t sector = f->start_lba + byte_offset / 512;
        uint32_t offset_in_sector = byte_offset % 512; // where to start

        ata_read_sectors(TAR_DRIVE, sector, 1, sector_buf);

        uint32_t chunk = 512 - offset_in_sector;
        if (chunk > nbyte - bytes_copied) chunk = nbyte - bytes_copied;

        // start for buf, start for sector buf, len
        memcpy((uint8_t *)buf + bytes_copied, sector_buf + offset_in_sector, chunk);
        bytes_copied += chunk;
    }

    f->position += bytes_copied;
    return bytes_copied;
}

int64_t tar_write(int fs_file_id, const void *buf, uint32_t nbyte) {
    return -1;
}

int tar_opendir(const char *path) {
    for (int i = 0; i < TAR_MAX_OPEN_DIRS; i++) {
        if (tar_open_dirs[i].in_use) continue;

        tar_open_dirs[i].in_use = 1;
        tar_open_dirs[i].current_lba = 0;

        size_t len = tar_strlen(path);
        if (len == 0) {
            tar_open_dirs[i].prefix[0] = '\0'; // root: no prefix to strip
        } else if (path[len - 1] == '/') {
            strcpy(tar_open_dirs[i].prefix, path);
        } else {
            npf_snprintf(tar_open_dirs[i].prefix, sizeof(tar_open_dirs[i].prefix), "%s/", path);
        }

        return i;
    }

    return -1; // table full
}

int tar_readdir(int fs_dir_id, vfs_dirent_t *out) {
    if (fs_dir_id < 0 || fs_dir_id >= TAR_MAX_OPEN_DIRS || !tar_open_dirs[fs_dir_id].in_use)
        return -1;

    tar_dir_t *d = &tar_open_dirs[fs_dir_id];
    size_t prefix_len = tar_strlen(d->prefix);

    ustar_header_t header;

    while (1) {
        ata_read_sectors(TAR_DRIVE, d->current_lba, 1, &header);

        if (is_zeroed(&header)) {
            return 0; // reached end of archive, nothing left to list
        }

        char full_name[256];
        tar_header_name(&header, full_name, sizeof(full_name));

        uint32_t file_size = octascii_to_dec(header.size, 12);
        uint32_t data_lba = d->current_lba + 1;
        uint32_t next_lba = data_lba + (file_size + 511) / 512;

        // not inside the directory we're listing? skip it
        if (strncmp(full_name, d->prefix, prefix_len) != 0) {
            d->current_lba = next_lba;
            continue;
        }

        const char *rel = full_name + prefix_len;

        // skip its own entry
        if (rel[0] == '\0') {
            d->current_lba = next_lba;
            continue;
        }

        // skip nested dirs
        if (tar_has_slash(rel)) {
            d->current_lba = next_lba;
            continue;
        }

        // found a direct child
        size_t name_len = tar_strlen(rel);
        if (name_len > 0 && rel[name_len - 1] == '/') name_len--; // drop a directory's own trailing slash
        if (name_len >= VFS_PATH_LENGTH) name_len = VFS_PATH_LENGTH - 1;
        memcpy(out->name, rel, name_len);
        out->name[name_len] = '\0';
        out->size = file_size;
        out->is_dir = (header.typeflag[0] == '5');

        d->current_lba = next_lba;
        return 1;
    }
}

int tar_closedir(int fs_dir_id) {
    if (fs_dir_id < 0 || fs_dir_id >= TAR_MAX_OPEN_DIRS || !tar_open_dirs[fs_dir_id].in_use)
        return -1;

    tar_open_dirs[fs_dir_id].in_use = 0;
    return 0;
}
