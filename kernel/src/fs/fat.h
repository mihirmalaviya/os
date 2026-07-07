#pragma once
#include <stdint.h>

// reads the boot sector from LBA 0 on the given drive and parses the FAT16 BPB
void fat_init(uint8_t drive);

// reads and prints every entry in the root directory
void fat_read_root_dir(void);

// looks up the next cluster in the chain from the in-memory FAT cache
// caller should compare the result against 0xFFF8 (end of chain) / 0xFFF7 (bad cluster)
uint16_t fat_next_cluster(uint16_t cluster);

// reads a whole file's cluster chain into a heap buffer (rounded up to a cluster boundary);
// only the first file_size bytes of the returned buffer are real file data
uint8_t *fat_read_file(uint16_t first_cluster, uint32_t file_size);

// looks up a file in the root directory by its raw 11-byte 8.3 name (e.g. "BIGFILE TXT");
// returns 1 and fills cluster/file_size if found, 0 otherwise
int fat_find_file(const char *name, uint16_t *cluster, uint32_t *file_size);
