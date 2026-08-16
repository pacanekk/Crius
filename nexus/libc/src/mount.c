/*
 * Nexus libc - mount and block device wrappers.
 */

#include <mount.h>
#include "nexus/syscall.h"

int mount(const char *device, const char *path) {
    return (int)_syscall2(SYS_MOUNT, (long)device, (long)path);
}

int umount(const char *path) {
    return (int)_syscall1(SYS_UMOUNT, (long)path);
}

int mount_count(void) {
    return (int)_syscall0(SYS_MOUNT_COUNT);
}

char *mount_point(int index) {
    return (char *)_syscall1(SYS_MOUNT_POINT, (long)index);
}

char *mount_device(int index) {
    return (char *)_syscall1(SYS_MOUNT_DEVICE, (long)index);
}
