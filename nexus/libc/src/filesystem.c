/*
 * Nexus libc - filesystem wrappers.
 */

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <crius/abi.h>
#include "nexus/syscall.h"

int open(const char *path, int flags) {
    long ret = _syscall2(SYS_OPEN, (long)path, (long)flags);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int close(int fd) {
    long ret = _syscall1(SYS_CLOSE, (long)fd);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int ioctl(int fd, unsigned long request, void *arg) {
    long ret = _syscall3(SYS_IOCTL, (long)fd, (long)request, (long)arg);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

ssize_t read(int fd, void *buf, size_t count) {
    long ret = _syscall3(SYS_READ, (long)fd, (long)buf, (long)count);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (ssize_t)ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    long ret = _syscall3(SYS_WRITE, (long)fd, (long)buf, (long)count);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (ssize_t)ret;
}

int mkdir(const char *path) {
    long ret = _syscall1(SYS_MKDIR, (long)path);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int unlink(const char *path) {
    long ret = _syscall1(SYS_UNLINK, (long)path);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int stat(const char *path, struct stat *st) {
    long ret = _syscall2(SYS_STAT, (long)path, (long)st);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return 0;
}

int chdir(const char *path) {
    long ret = _syscall1(SYS_CHDIR, (long)path);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int getcwd(char *buf, size_t size) {
    long ret = _syscall2(SYS_GETCWD, (long)buf, (long)size);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return 0;
}

/* Create a file - FD-based: open with O_CREAT, then close */
int create(const char *path) {
    int fd = open(path, O_CREAT | O_WRONLY);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

/* Write data to a path - FD-based: open, write, close */
int write_file(const char *path, const char *data, size_t len) {
    int fd = open(path, O_CREAT | O_WRONLY);
    if (fd < 0) return -1;
    int ret = (int)write(fd, data, len);
    close(fd);
    return ret;
}

/* Directory operations - POSIX style */
#define MAX_DIRS 8

struct __dirstream {
    int fd;
    int in_use;
    char path[256];
    struct dirent ent;
};

static struct __dirstream dirs[MAX_DIRS];

DIR *opendir(const char *path) {
    int free_idx = -1;
    for (int i = 0; i < MAX_DIRS; i++) {
        if (!dirs[i].in_use) { free_idx = i; break; }
    }
    if (free_idx < 0) { errno = ENOMEM; return NULL; }

    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;

    int i = 0;
    for (; path[i] && i < 255; i++) dirs[free_idx].path[i] = path[i];
    dirs[free_idx].path[i] = '\0';
    dirs[free_idx].fd = fd;
    dirs[free_idx].in_use = 1;
    return (DIR *)&dirs[free_idx];
}

struct dirent *readdir(DIR *dir) {
    if (!dir) { errno = EBADF; return NULL; }
    struct __dirstream *d = (struct __dirstream *)dir;
    if (!d->in_use || d->fd < 0) { errno = EBADF; return NULL; }

    ssize_t n = read(d->fd, &d->ent, sizeof(struct dirent));
    if (n == (ssize_t)sizeof(struct dirent)) return &d->ent;
    if (n == 0) return NULL;
    if (n < 0) return NULL;   /* read() already set errno */
    errno = EINVAL;
    return NULL;
}

int closedir(DIR *dir) {
    if (!dir) { errno = EBADF; return -1; }
    struct __dirstream *d = (struct __dirstream *)dir;
    if (!d->in_use) { errno = EBADF; return -1; }

    int ret = close(d->fd);
    d->in_use = 0;
    d->fd = -1;
    return ret;
}

void rewinddir(DIR *dir) {
    if (!dir) return;
    struct __dirstream *d = (struct __dirstream *)dir;
    if (!d->in_use) return;

    int newfd = open(d->path, O_RDONLY);
    if (newfd >= 0) {
        close(d->fd);
        d->fd = newfd;
    }
}
