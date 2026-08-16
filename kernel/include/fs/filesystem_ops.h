#ifndef FILESYSTEM_OPS_H
#define FILESYSTEM_OPS_H

#include <stddef.h>
#include <stdint.h>
#include "file.h"

struct block_device;
struct vfs_fs_info;

/* Filesystem operations vtable - implemented by each filesystem.
 * All path arguments are relative to the mount point (already stripped). */
struct filesystem_ops {
    const char *name;

    int (*mount)(struct block_device *device, void **fs_private);

    int (*open)(void *fs_private, const char *path, int flags, struct file *file);

    int (*stat)(void *fs_private, const char *path, int *type, size_t *size);

    int (*create)(void *fs_private, const char *path);

    int (*mkdir)(void *fs_private, const char *path);

    int (*unlink)(void *fs_private, const char *path);

    int (*read_at)(void *fs_private, const char *path, uint64_t offset, void *buffer, size_t size);

    int (*write_at)(void *fs_private, const char *path, uint64_t offset, const void *buffer, size_t size);

    int (*truncate)(void *fs_private, const char *path);

    int (*readdir)(void *fs_private, const char *path, uint64_t index, struct dirent *out);

    /* Optional: query filesystem statistics (block size, block/inode counts) */
    int (*info)(void *fs_private, struct vfs_fs_info *out);

    /* Optional: unmount filesystem, free fs_private and resources */
    int (*unmount)(void *fs_private);
};

#endif
