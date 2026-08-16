#ifndef RAMFS_H
#define RAMFS_H

#include <stddef.h>
#include "fs/file.h"
#include "fs/vfs.h"

#define MAX_INODES   256
#define MAX_NAME     32
#define MAX_FILESIZE 65536
#define MAX_DIR_ENT  32

#define T_FILE 1
#define T_DIR  2
#define T_DEV  3

typedef int (*dev_read_fn)(char *buf, size_t bufsize);
typedef int (*dev_write_fn)(const char *data, size_t len);
typedef int (*dev_ioctl_fn)(unsigned long request, void *arg);

/* ===== Public API ===== */

void ramfs_init(void);

/* Filesystem operations table */
extern const struct filesystem_ops ramfs_fs_ops;

/* ===== Device creation functions (used by devfs/procfs) ===== */

int ramfs_mkdir(const char *path);
int ramfs_create_dev(const char *path, dev_read_fn rfn, dev_write_fn wfn);
int ramfs_create_dev_ioctl(const char *path, dev_read_fn rfn, dev_write_fn wfn, dev_ioctl_fn ifn);
int ramfs_create_dev_stream(const char *path, dev_read_fn rfn, dev_write_fn wfn);

#endif
