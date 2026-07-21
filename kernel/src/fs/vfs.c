#include "fs/vfs.h"
#include "fs/tar.h"
#include "fs/proc.h"
#include "mm/heap.h"
#include "sched/task.h"
#include <string.h>

mountpoint_t *mountpoints_root;

static fs_operations_t tar_ops = {
    tar_open, tar_close, tar_read, tar_write,
    tar_opendir, tar_readdir, tar_closedir
};
static fs_operations_t proc_ops = {
    proc_open, proc_close, proc_read, proc_write,
    proc_opendir, proc_readdir, proc_closedir
};

static fs_operations_t *resolve_fs_ops(const char *fs_type) {
    if (strcmp(fs_type, "tar") == 0) return &tar_ops;
    if (strcmp(fs_type, "proc") == 0) return &proc_ops;
    return NULL;
}

int vfs_mount(char *device, char *target, char *fs_type) {
    mountpoint_t *new_mountpoint = kmalloc(sizeof(mountpoint_t));
    if (new_mountpoint == NULL)
        return -1;

    strcpy(new_mountpoint->device, device);
    strcpy(new_mountpoint->type, fs_type);
    strcpy(new_mountpoint->mountpoint, target);
    new_mountpoint->operations = resolve_fs_ops(fs_type);

    new_mountpoint->next = mountpoints_root;
    mountpoints_root = new_mountpoint;

    return 0;
}

int vfs_umount(char *device, char *target){
    mountpoint_t *curr = mountpoints_root;
    mountpoint_t *prev = NULL;
    while (curr != NULL) {
        if (strcmp(curr->mountpoint, target) == 0) {
            if (prev == NULL) {
                mountpoints_root = curr->next;
            } else {
                prev->next = curr->next;
            }
            kfree(curr);
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }

    return 0;
}

mountpoint_t *get_mountpoint(const char *path) {
    mountpoint_t *best = NULL;
    int best_len = -1;

    mountpoint_t *curr = mountpoints_root;
    while (curr != NULL) {
        int len = 0;
        while (curr->mountpoint[len] != '\0') len++;

        if (len > best_len && strncmp(path, curr->mountpoint, len) == 0) {
            best = curr;
            best_len = len;
        }
        curr = curr->next;
    }

    return best;
}

static vfs_file_t vfs_open_files[VFS_MAX_OPEN_FILES];

// resolve an fd to its open-file slot, or NULL if the fd is invalid/unused
static vfs_file_t *get_file(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || vfs_open_files[fd].fs_file_id < 0)
        return NULL;
    return &vfs_open_files[fd];
}

// strips the mountpoint prefix off path
static const char *get_rel_path(mountpoint_t *mountpoint, const char *path) {
    int len = 0;
    while (mountpoint->mountpoint[len] != '\0') len++;

    const char *rel = path + len;
    while (*rel == '/') rel++;

    return rel;
}

int open(const char *path, int flags) {
    mountpoint_t *mountpoint = get_mountpoint(path);
    if (mountpoint == NULL || mountpoint->operations == NULL)
        return -1;

    const char *rel_path = get_rel_path(mountpoint, path);
    int fs_file_id = mountpoint->operations->open(rel_path, flags);
    if (fs_file_id < 0)
        return -1;

    // find an empty slot and add it
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i].fs_file_id < 0) {
            vfs_open_files[i].fs_file_id = fs_file_id;
            vfs_open_files[i].mountpoint = mountpoint;
            return i;
        }
    }

    // TODO dynamic array

    // no free vfs descriptor slots - undo the driver-level open
    mountpoint->operations->close(fs_file_id);
    return -1;
}

int close(int fd) {
    vfs_file_t *f = get_file(fd);
    if (!f) return -1;

    int result = f->mountpoint->operations->close(f->fs_file_id);

    f->fs_file_id = -1;
    f->mountpoint = NULL;

    return result;
}

int64_t read(int fd, void *buf, uint32_t nbyte) {
    vfs_file_t *f = get_file(fd);
    if (!f) return -1;
    return f->mountpoint->operations->read(f->fs_file_id, buf, nbyte);
}

int64_t write(int fd, const void *buf, uint32_t nbyte) {
    vfs_file_t *f = get_file(fd);
    if (!f) return -1;
    return f->mountpoint->operations->write(f->fs_file_id, buf, nbyte);
}

// reserve a fd for something with no path
int vfs_install_fd(mountpoint_t *mp, int fs_file_id) {
    lock_stuff();
    for (int i=0; i<VFS_MAX_OPEN_FILES; i++){
        if (vfs_open_files[i].fs_file_id<0){
            vfs_open_files[i].fs_file_id = fs_file_id;
            vfs_open_files[i].mountpoint = mp;
            unlock_stuff();
            return i;
        }
    }
    unlock_stuff();
    return -1;
}

// map an fd back to its id
int vfs_fs_id(int fd) {
    vfs_file_t *f = get_file(fd);
    if (!f) 
        return -1;
    return f->fs_file_id;
}

int opendir(const char *path) {
    mountpoint_t *mountpoint = get_mountpoint(path);
    if (mountpoint==NULL || mountpoint->operations==NULL)
        return -1;

    const char *rel_path = get_rel_path(mountpoint, path);
    int fs_dir_id = mountpoint->operations->opendir(rel_path);
    if (fs_dir_id<0)
        return -1;

    for (int i=0; i<VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i].fs_file_id < 0) {
            vfs_open_files[i].fs_file_id = fs_dir_id;
            vfs_open_files[i].mountpoint = mountpoint;
            return i;
        }
    }

    mountpoint->operations->closedir(fs_dir_id);
    return -1;
}

int closedir(int fd) {
    vfs_file_t *f = get_file(fd);
    if (!f) return -1;

    int result = f->mountpoint->operations->closedir(f->fs_file_id);

    f->fs_file_id = -1;
    f->mountpoint = NULL;

    return result;
}

int readdir(int fd, vfs_dirent_t *out) {
    vfs_file_t *f = get_file(fd);
    if (!f) return -1;
    return f->mountpoint->operations->readdir(f->fs_file_id, out);
}

void vfs_init(void) {
    for (int i=0; i<VFS_MAX_OPEN_FILES; i++) {
        vfs_open_files[i].fs_file_id = -1;
    }

    vfs_mount("", "/", "tar");
    vfs_mount("", "/proc", "proc");
}
