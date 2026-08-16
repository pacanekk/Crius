#ifndef FILE_H
#define FILE_H

#include <stddef.h>
#include <stdint.h>

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREATE    4
#define O_APPEND    8
#define O_TRUNC     16

#define FILE_TYPE_FILE  1
#define FILE_TYPE_DIR   2
#define FILE_TYPE_DEV   3

/* Directory entry returned by read() on a directory fd */
struct dirent {
    uint32_t inode;
    uint8_t type;
    char name[256];
};

struct file;

/* File operations vtable - implemented by each filesystem */
struct file_operations {
    int (*read)(struct file *f, char *buf, int count);
    int (*write)(struct file *f, const char *buf, int count);
    int (*ioctl)(struct file *f, unsigned long req, void *arg);
    int (*readdir)(struct file *f, struct dirent *de);
    void (*close)(struct file *f);
};

struct file {
    int used;
    int refcount;
    int flags;          /* O_RDONLY, O_WRONLY, ... */
    uint64_t offset;    /* read/write position */
    const struct file_operations *ops;
    void *priv;         /* FS-private data (owned by the implementation) */
};

/* Allocate a new file object (refcount=1) */
struct file *file_alloc(void);

/* Increase refcount (for fork/share) */
void file_get(struct file *f);

/* Decrease refcount, free if reaches 0 */
void file_put(struct file *f);

/* Read from file object into buf, returns bytes read */
int file_read(struct file *f, char *buf, int count);

/* Write to file object from buf, returns bytes written */
int file_write(struct file *f, const char *buf, int count);

/* Ioctl on file object, returns 0 on success, -1 on error */
int file_ioctl(struct file *f, unsigned long request, void *arg);

#endif
