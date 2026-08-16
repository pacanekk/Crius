#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>
#include "file.h"
#include "filesystem_ops.h"

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR  2
#define VFS_TYPE_DEV  3

#define VFS_MAX_MOUNTS 16

/* Filesystem statistics (for /dev/hda info, procfs, etc.) */
struct vfs_fs_info {
    int mounted;
    char fs_name[16];
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
};

/* Query filesystem info at a given mount path. Returns 0 on success, -1 if not found. */
int vfs_get_fs_info(const char *mount_path, struct vfs_fs_info *out);

void vfs_register_filesystem(struct filesystem_ops *ops);

void vfs_init(void);

/* Open a path and return a file object, or NULL on error */
struct file *vfs_open(const char *abs_path, int flags);

/* Generic directory readdir via file ops - delegates to fs_ops->readdir */
int vfs_dir_readdir(struct file *f, struct dirent *de);

/* Generic directory close - frees dir_handle allocated by vfs_open */
void vfs_dir_close(struct file *f);

int vfs_mkdir(const char *path);
int vfs_create(const char *path);
int vfs_write(const char *path, const char *data, size_t len);
int vfs_append(const char *path, const char *data, size_t len);
int vfs_write_at(const char *path, size_t offset, const char *data, size_t len);
int vfs_read(const char *path, char *buf, size_t bufsize);
int vfs_read_at(const char *path, size_t offset, char *buf, size_t count);
int vfs_truncate(const char *path);
int vfs_delete(const char *path);
int vfs_stat(const char *path, int *type, size_t *size);

typedef void (*vfs_list_cb)(const char *name, int type, size_t size);
int vfs_list(const char *path, vfs_list_cb cb);
int vfs_list_buf(const char *path, char *buf, int bufsize);

int vfs_mount(const char *device, const char *path);
int vfs_umount(const char *path);
int vfs_mount_count(void);
const char *vfs_mount_point(int idx);
const char *vfs_mount_device(int idx);

int vfs_chdir(const char *path);
void vfs_pwd(char *buf, int bufsize);
void vfs_resolve_abs(const char *path, char *out, int outsize);

/* Read one directory entry at given index. Returns 1 on success, 0 on EOF, -1 on error. */
struct dirent;
int vfs_readdir(const char *abs_path, int index, struct dirent *out);

#endif
