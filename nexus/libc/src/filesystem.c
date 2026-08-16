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
    int type;
    size_t size;
    long ret = _syscall3(SYS_STAT, (long)path, (long)&type, (long)&size);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    memset(st, 0, sizeof(*st));
    st->st_size = (off_t)size;
    switch (type) {
    case FILE_TYPE_FILE: st->st_mode = S_IFREG | 0644; break;
    case FILE_TYPE_DIR:  st->st_mode = S_IFDIR | 0755; break;
    case FILE_TYPE_DEV:  st->st_mode = S_IFBLK | 0660; break;
    }
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

/* Directory operations - FD-based */
int opendir(const char *path) {
    return open(path, O_RDONLY);
}

int readdir(int fd, struct dirent *entry) {
    ssize_t n = read(fd, entry, sizeof(struct dirent));
    if (n == (ssize_t)sizeof(struct dirent)) return 1;
    if (n == 0) return 0;
    if (n < 0) return -1;   /* read() already set errno */
    errno = EINVAL;
    return -1;
}
