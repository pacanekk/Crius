#ifndef RAMFS_INTERNAL_H
#define RAMFS_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "fs/ramfs.h"

/* ===== Private inode structure ===== */

struct ramfs_inode {
    int used;
    int type;
    char name[MAX_NAME];
    int parent;
    size_t size;
    char data[MAX_FILESIZE];
    int entries[MAX_DIR_ENT];
    int entry_count;
    dev_read_fn dev_read;
    dev_write_fn dev_write;
    dev_ioctl_fn dev_ioctl;
    int stream;
};

/* ===== Private functions ===== */

int ramfs_create(const char *path);
struct ramfs_inode *ramfs_get_inode(int idx);
int ramfs_resolve(const char *path, int *out_inode);
int ramfs_chdir(const char *path);
void ramfs_pwd(char *buf, int bufsize);
int ramfs_write(const char *path, const char *data, size_t len);
int ramfs_append(const char *path, const char *data, size_t len);
int ramfs_write_at(int idx, size_t offset, const char *data, size_t len);
int ramfs_read_at(int idx, size_t offset, char *buf, size_t count);
int ramfs_read(const char *path, char *buf, size_t bufsize);
int ramfs_delete(const char *path);
int ramfs_list_dir(const char *path, int *out_indices, int max);
int ramfs_stat(const char *path, int *type, size_t *size);

/* ===== Private file operations tables ===== */

extern const struct file_operations ramfs_file_ops;
extern const struct file_operations ramfs_dev_ops;
extern const struct file_operations ramfs_dir_ops;

#endif
