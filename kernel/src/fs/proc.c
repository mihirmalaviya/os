#include "stdbool.h"
#include "sched/task.h"
#include "fs/proc.h"
#include <string.h>

#define MAX_PROC_HANDLES 8

typedef struct {
    int in_use;
    int read_pos; // how many bytes of the generated text have already been returned
} proc_handle_t;

typedef struct {
    int in_use;
    int index; // which known_entries[] slot readdir will return next
} proc_dir_iter_t;

static proc_handle_t proc_handles[MAX_PROC_HANDLES];
static proc_dir_iter_t proc_dir_iters[MAX_PROC_HANDLES];

int proc_open(const char *path, int flags) {
    return -1;
}

int proc_close(int fs_file_id) {
    return -1;
}

int64_t proc_read(int fs_file_id, void *buf, uint32_t nbyte) {
    return -1;
}

int64_t proc_write(int fs_file_id, const void *buf, uint32_t nbyte) {
    return -1;
}

int proc_opendir(const char *path) {
    if (strcmp(path, "") != 0) {
        return -1; // only /proc for now
    }

    for (int i = 0; i < MAX_PROC_HANDLES; i++) {
        if (!proc_dir_iters[i].in_use) {
            proc_dir_iters[i].in_use = 1;
            proc_dir_iters[i].index = 0;
            return i;
        }
    }

    return -1;
}

int proc_readdir(int fs_dir_id, vfs_dirent_t *out) {
    int idx = proc_dir_iters[fs_dir_id].index;

    // out.name = ;
    // out.is_dir = false;
    // out.size = 0;

    proc_dir_iters[fs_dir_id].index += 1;
    return 0;
}

int proc_closedir(int fs_dir_id) {
    if (fs_dir_id < 0 || fs_dir_id >= MAX_PROC_HANDLES || !proc_dir_iters[fs_dir_id].in_use) {
        return -1;
    }

    proc_dir_iters[fs_dir_id].in_use = 0;
    return 0;
}
