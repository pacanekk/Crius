/*
 * Nexus libc - mount and block device wrappers.
 */

#include <errno.h>
#include <mount.h>
#include "nexus/syscall.h"

int mount(const char *device, const char *path) {
    long ret = _syscall2(SYS_MOUNT, (long)device, (long)path);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int umount(const char *path) {
    long ret = _syscall1(SYS_UMOUNT, (long)path);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int mount_count(void) {
    return (int)_syscall0(SYS_MOUNT_COUNT);
}

char *mount_point(int index) {
    long ret = _syscall1(SYS_MOUNT_POINT, (long)index);
    if (ret < 0) { errno = (int)(-ret); return 0; }
    return (char *)ret;
}

char *mount_device(int index) {
    long ret = _syscall1(SYS_MOUNT_DEVICE, (long)index);
    if (ret < 0) { errno = (int)(-ret); return 0; }
    return (char *)ret;
}
