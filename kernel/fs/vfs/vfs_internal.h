#ifndef VFS_INTERNAL_H
#define VFS_INTERNAL_H

#include <stdint.h>
#include "fs/vfs.h"
#include "drivers/block_device.h"

#define MAX_FS_TYPES 8
extern struct filesystem_ops *registered_fs[MAX_FS_TYPES];

struct vfs_mount {
    int used;
    char path[128];
    char device[64];
    struct filesystem_ops *ops;
    void *fs_private;
    struct block_device *device_bdev;
};

extern struct vfs_mount mounts[VFS_MAX_MOUNTS];

struct dir_handle {
    uint64_t position;
    struct vfs_mount *mount;
    char rel_path[256];
};

int find_mounts(const char *path, struct vfs_mount **out, int max);
struct vfs_mount *find_mount(const char *path);
const char *strip_mount(const char *path, struct vfs_mount *m);
void resolve_abs(const char *path, char *out, int outsize);

#endif
