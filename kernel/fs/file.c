#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs/file.h"

#define MAX_FILES 2048

static struct file file_pool[MAX_FILES];

struct file *file_alloc(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_pool[i].used) {
            memset(&file_pool[i], 0, sizeof(struct file));
            file_pool[i].used = 1;
            file_pool[i].refcount = 1;
            file_pool[i].ops = NULL;
            file_pool[i].priv = NULL;
            return &file_pool[i];
        }
    }
    return NULL;
}

void file_get(struct file *f) {
    if (f) f->refcount++;
}

void file_put(struct file *f) {
    if (!f) return;
    f->refcount--;
    if (f->refcount <= 0) {
        if (f->ops && f->ops->close)
            f->ops->close(f);
        f->used = 0;
        f->refcount = 0;
        f->offset = 0;
        f->ops = NULL;
        f->priv = NULL;
    }
}

int file_read(struct file *f, char *buf, int count) {
    if (!f || !f->used) return -EBADF;
    if (!f->ops) return -EBADF;
    if (f->ops->readdir) {
        if (count < (int)sizeof(struct dirent)) return -EINVAL;
        struct dirent *de = (struct dirent *)buf;
        int ret = f->ops->readdir(f, de);
        if (ret <= 0) return ret;
        return (int)sizeof(struct dirent);
    }
    if (!f->ops->read) return -EBADF;
    return f->ops->read(f, buf, count);
}

int file_write(struct file *f, const char *buf, int count) {
    if (!f || !f->used) return -EBADF;
    if (!f->ops || !f->ops->write) return -EBADF;
    return f->ops->write(f, buf, count);
}

int file_ioctl(struct file *f, unsigned long request, void *arg) {
    if (!f || !f->used) return -EBADF;
    if (!f->ops || !f->ops->ioctl) return -EBADF;
    return f->ops->ioctl(f, request, arg);
}
