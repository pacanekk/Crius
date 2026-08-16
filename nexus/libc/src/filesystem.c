/*
 * Nexus libc - filesystem wrappers.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "nexus/syscall.h"

int open(const char *path, int flags) {
    return (int)_syscall2(SYS_OPEN, (long)path, (long)flags);
}

int close(int fd) {
    return (int)_syscall1(SYS_CLOSE, (long)fd);
}

int ioctl(int fd, unsigned long request, void *arg) {
    return (int)_syscall3(SYS_IOCTL, (long)fd, (long)request, (long)arg);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)_syscall3(SYS_READ, (long)fd, (long)buf, (long)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)_syscall3(SYS_WRITE, (long)fd, (long)buf, (long)count);
}

int mkdir(const char *path) {
    return (int)_syscall1(SYS_MKDIR, (long)path);
}

int unlink(const char *path) {
    return (int)_syscall1(SYS_UNLINK, (long)path);
}

int stat(const char *path, struct stat *st) {
    int type;
    size_t size;
    long ret = _syscall3(SYS_STAT, (long)path, (long)&type, (long)&size);
    if (ret < 0) return -1;
    st->type = type;
    st->size = size;
    return 0;
}

int chdir(const char *path) {
    return (int)_syscall1(SYS_CHDIR, (long)path);
}

int getcwd(char *buf, size_t size) {
    _syscall2(SYS_GETCWD, (long)buf, (long)size);
    return 0;
}

/* Create a file - FD-based: open with O_CREATE, then close */
int create(const char *path) {
    int fd = open(path, O_CREATE | O_WRONLY);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

/* Write data to a path - FD-based: open, write, close */
int write_file(const char *path, const char *data, size_t len) {
    int fd = open(path, O_CREATE | O_WRONLY);
    if (fd < 0) return -1;
    int ret = (int)write(fd, data, len);
    close(fd);
    return ret;
}

/* Directory operations - FD-based */
int opendir(const char *path) {
    return open(path, O_RDONLY);
}

int readdir(int fd, struct dirent *entry) {
    ssize_t n = read(fd, entry, sizeof(struct dirent));
    if (n == (ssize_t)sizeof(struct dirent)) return 1;
    if (n == 0) return 0;
    return -1;
}
