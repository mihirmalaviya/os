#pragma once
#import	<stdint.h>
#define VFS_TYPE_LENGTH 32
#define VFS_PATH_LENGTH 256 // must cover the largest path any backing fs can produce (ustar: 100 name + 155 prefix)

// one entry as returned by readdir
typedef struct {
    char name[VFS_PATH_LENGTH];
    uint32_t size;
    uint8_t is_dir;
} vfs_dirent_t;

typedef struct fs_operations_t {
    int (*open)(const char *path, int flags);
    int (*close)(int fs_file_id);
    int64_t (*read)(int fs_file_id, void *buf, uint32_t nbyte);
    int64_t (*write)(int fs_file_id, const void *buf, uint32_t nbyte);

    int (*opendir)(const char *path);
    int (*readdir)(int fs_dir_id, vfs_dirent_t *out);
    int (*closedir)(int fs_dir_id);

} fs_operations_t;


#define MAX_MOUNTPOINTS 12

typedef struct mountpoint_t {
    char type[VFS_TYPE_LENGTH];
    char device[VFS_PATH_LENGTH];
    char mountpoint[VFS_PATH_LENGTH];

		// Driver provided file system operations (open/read/write/close)
    fs_operations_t *operations;

    struct mountpoint_t *next;
} mountpoint_t;

extern mountpoint_t *mountpoints_root;

// mountpoint_t *create_mountpoint(...);
// mountpoint_t *add_mountpoint(mountpoint_t* mountpoint);
// void remove_mountpoint(mountpoint_t* mountpoint);

int vfs_mount(char *device, char *target, char *fs_type);
int vfs_umount(char *device, char *target);
mountpoint_t *get_mountpoint(const char *path);

#define VFS_MAX_OPEN_FILES 64

// a VFS-level handle to an open file: which mountpoint owns it, and the
// driver's own fs_specific id for it (from fs_operations_t.open)
typedef struct {
    int fs_file_id; // -1 means this slot is free
    mountpoint_t *mountpoint;
} vfs_file_t;

int open(const char *path, int flags);
int close(int fd);
int64_t read(int fd, void *buf, uint32_t nbyte);

int opendir(const char *path);
int closedir(int fd);
int readdir(int fd, vfs_dirent_t *out);

// mounts the root filesystem; must be called once before any open()/read()/etc.
void vfs_init(void);
