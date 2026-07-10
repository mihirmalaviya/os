#pragma once
#include <stdint.h>
#include "fs/vfs.h"

int proc_open(const char *path, int flags);
int proc_close(int fs_file_id);
int64_t proc_read(int fs_file_id, void *buf, uint32_t nbyte);
int64_t proc_write(int fs_file_id, const void *buf, uint32_t nbyte);

int proc_opendir(const char *path);
int proc_readdir(int fs_dir_id, vfs_dirent_t *out);
int proc_closedir(int fs_dir_id);
