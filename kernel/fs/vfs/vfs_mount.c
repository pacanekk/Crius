#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs/vfs.h"
#include "vfs_internal.h"
#include "drivers/block_device.h"

/* ===== Mount system ===== */

int vfs_mount(const char *device, const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));

    /* Allow overlapping mounts (e.g. ext2 at / on top of ramfs at /).
     * Only reject if the same device is already mounted at the same path. */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && strcmp(mounts[i].path, abs) == 0 &&
            strcmp(mounts[i].device, device) == 0)
            return -1;
    }

    const char *dev_name = device;
    if (strncmp(dev_name, "/dev/", 5) == 0) dev_name += 5;

    struct block_device *bd = block_device_find(dev_name);
    if (!bd) return -1;

    /* Try each registered filesystem type (skip ramfs for block devices) */
    for (int i = 0; i < MAX_FS_TYPES; i++) {
        if (!registered_fs[i]) continue;
        if (strcmp(registered_fs[i]->name, "ramfs") == 0) continue;
        void *fs_priv = NULL;
        if (registered_fs[i]->mount(bd, &fs_priv) == 0) {
            for (int j = 0; j < VFS_MAX_MOUNTS; j++) {
                if (!mounts[j].used) {
                    mounts[j].used = 1;
                    int plen = 0;
                    while (abs[plen] && plen < 127) plen++;
                    memcpy(mounts[j].path, abs, plen);
                    mounts[j].path[plen] = '\0';
                    int dlen = 0;
                    while (device[dlen] && dlen < 63) dlen++;
                    memcpy(mounts[j].device, device, dlen);
                    mounts[j].device[dlen] = '\0';
                    mounts[j].ops = registered_fs[i];
                    mounts[j].fs_private = fs_priv;
                    mounts[j].device_bdev = bd;
                    return 0;
                }
            }
            return -1;
        }
    }

    return -1;
}

int vfs_umount(const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && strcmp(mounts[i].path, abs) == 0) {
            if (mounts[i].ops && mounts[i].ops->unmount)
                mounts[i].ops->unmount(mounts[i].fs_private);
            mounts[i].used = 0;
            mounts[i].ops = NULL;
            mounts[i].fs_private = NULL;
            mounts[i].device_bdev = NULL;
            return 0;
        }
    }
    return -1;
}

int vfs_mount_count(void) {
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        if (mounts[i].used) n++;
    return n;
}

const char *vfs_mount_point(int idx) {
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used) {
            if (n == idx) return mounts[i].path;
            n++;
        }
    }
    return NULL;
}

const char *vfs_mount_device(int idx) {
    int n = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used) {
            if (n == idx) return mounts[i].device;
            n++;
        }
    }
    return NULL;
}
