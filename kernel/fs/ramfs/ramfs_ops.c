#include <stdint.h>
#include <string.h>
#include "fs/ramfs.h"
#include "ramfs_internal.h"
#include "fs/vfs.h"

/* ===== ramfs file operations ===== */

static int ramfs_file_read(struct file *f, char *buf, int count) {
    int idx = (int)(intptr_t)f->priv;
    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino) return -1;
    if (f->offset >= ino->size) return 0;
    int avail = (int)(ino->size - f->offset);
    if (avail > count) avail = count;
    memcpy(buf, ino->data + f->offset, avail);
    f->offset += avail;
    return avail;
}

static int ramfs_file_write(struct file *f, const char *buf, int count) {
    int idx = (int)(intptr_t)f->priv;
    size_t write_offset = f->offset;
    if (f->flags & O_APPEND) {
        struct ramfs_inode *ino = ramfs_get_inode(idx);
        if (!ino) return -1;
        write_offset = ino->size;
    }
    int ret = ramfs_write_at(idx, write_offset, buf, count);
    if (ret >= 0) f->offset = write_offset + count;
    return ret;
}

static int ramfs_dev_read(struct file *f, char *buf, int count) {
    int idx = (int)(intptr_t)f->priv;
    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino || !ino->dev_read) return -1;
    if (!ino->stream && f->offset > 0) return 0;
    int n = ino->dev_read(buf, count);
    if (n > 0) f->offset += n;
    return n;
}

static int ramfs_dev_write(struct file *f, const char *buf, int count) {
    int idx = (int)(intptr_t)f->priv;
    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino || !ino->dev_write) return -1;
    return ino->dev_write(buf, count);
}

static int ramfs_dev_ioctl(struct file *f, unsigned long req, void *arg) {
    int idx = (int)(intptr_t)f->priv;
    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino || !ino->dev_ioctl) return -1;
    return ino->dev_ioctl(req, arg);
}

const struct file_operations ramfs_file_ops = {
    .read    = ramfs_file_read,
    .write   = ramfs_file_write,
    .ioctl   = NULL,
    .readdir = NULL,
    .close   = NULL,
};

const struct file_operations ramfs_dev_ops = {
    .read    = ramfs_dev_read,
    .write   = ramfs_dev_write,
    .ioctl   = ramfs_dev_ioctl,
    .readdir = NULL,
    .close   = NULL,
};

const struct file_operations ramfs_dir_ops = {
    .read    = NULL,
    .write   = NULL,
    .ioctl   = NULL,
    .readdir = vfs_dir_readdir,
    .close   = vfs_dir_close,
};

/* ===== ramfs filesystem operations ===== */

static int ramfs_fs_mount(struct block_device *device, void **fs_private) {
    (void)device;
    /* ramfs is always available - no device needed */
    *fs_private = NULL;
    return 0;
}

static int ramfs_fs_open(void *fs_private, const char *path, int flags, struct file *file) {
    (void)fs_private;
    int idx;
    if (ramfs_resolve(path, &idx) < 0) {
        if (flags & O_CREAT) {
            if (ramfs_create(path) < 0) return -1;
            if (ramfs_resolve(path, &idx) < 0) return -1;
        } else {
            return -1;
        }
    }

    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino) return -1;

    if ((flags & O_TRUNC) && ino->type == T_FILE) {
        ino->size = 0;
    }

    file->priv = (void *)(intptr_t)idx;
    if (ino->type == T_DEV)
        file->ops = &ramfs_dev_ops;
    else if (ino->type == T_DIR)
        file->ops = &ramfs_dir_ops;
    else
        file->ops = &ramfs_file_ops;

    return 0;
}

static int ramfs_fs_stat(void *fs_private, const char *path, int *type, size_t *size) {
    (void)fs_private;
    int idx;
    if (ramfs_resolve(path, &idx) < 0) return -1;
    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino) return -1;
    if (type) *type = ino->type;
    if (size) *size = ino->size;
    return 0;
}

static int ramfs_fs_create(void *fs_private, const char *path) {
    (void)fs_private;
    return ramfs_create(path);
}

static int ramfs_fs_mkdir(void *fs_private, const char *path) {
    (void)fs_private;
    return ramfs_mkdir(path);
}

static int ramfs_fs_unlink(void *fs_private, const char *path) {
    (void)fs_private;
    return ramfs_delete(path);
}

static int ramfs_fs_read_at(void *fs_private, const char *path, uint64_t offset, void *buffer, size_t size) {
    (void)fs_private;
    int idx;
    if (ramfs_resolve(path, &idx) < 0) return -1;
    return ramfs_read_at(idx, (size_t)offset, buffer, size);
}

static int ramfs_fs_write_at(void *fs_private, const char *path, uint64_t offset, const void *buffer, size_t size) {
    (void)fs_private;
    int idx;
    if (ramfs_resolve(path, &idx) < 0) {
        if (ramfs_create(path) < 0) return -1;
        if (ramfs_resolve(path, &idx) < 0) return -1;
    }
    return ramfs_write_at(idx, (size_t)offset, buffer, size);
}

static int ramfs_fs_truncate(void *fs_private, const char *path) {
    (void)fs_private;
    int idx;
    if (ramfs_resolve(path, &idx) < 0) return -1;
    struct ramfs_inode *ino = ramfs_get_inode(idx);
    if (!ino) return -1;
    ino->size = 0;
    return 0;
}

static int ramfs_fs_readdir(void *fs_private, const char *path, uint64_t index, struct dirent *out) {
    (void)fs_private;
    int idx;
    if (ramfs_resolve(path, &idx) < 0) return -1;
    struct ramfs_inode *dir = ramfs_get_inode(idx);
    if (!dir || dir->type != T_DIR) return -1;
    if ((int)index >= dir->entry_count) return 0;
    int child_idx = dir->entries[index];
    struct ramfs_inode *child = ramfs_get_inode(child_idx);
    if (!child) return -1;
    out->inode = (uint32_t)child_idx;
    out->type = (uint8_t)child->type;
    int i = 0;
    while (child->name[i] && i < 255) { out->name[i] = child->name[i]; i++; }
    out->name[i] = '\0';
    return 1;
}

const struct filesystem_ops ramfs_fs_ops = {
    .name     = "ramfs",
    .mount    = ramfs_fs_mount,
    .open     = ramfs_fs_open,
    .stat     = ramfs_fs_stat,
    .create   = ramfs_fs_create,
    .mkdir    = ramfs_fs_mkdir,
    .unlink   = ramfs_fs_unlink,
    .read_at  = ramfs_fs_read_at,
    .write_at = ramfs_fs_write_at,
    .truncate = ramfs_fs_truncate,
    .readdir  = ramfs_fs_readdir,
};
