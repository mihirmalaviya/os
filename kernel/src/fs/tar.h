#pragma once
#include <stdint.h>
#include <stddef.h>
#include "fs/vfs.h"

int tar_open(const char *path, int flags);
int tar_close(int fs_file_id);
int64_t tar_read(int fs_file_id, void *buf, uint32_t nbyte);
int64_t tar_write(int fs_file_id, const void *buf, uint32_t nbyte);

int tar_opendir(const char *path);
int tar_readdir(int fs_dir_id, vfs_dirent_t *out);
int tar_closedir(int fs_dir_id);

size_t octascii_to_dec(char *number, int size);
