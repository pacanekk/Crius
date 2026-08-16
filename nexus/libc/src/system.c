/*
 * Nexus libc - system wrappers.
 */

#include <unistd.h>
#include <crius/abi.h>
#include "nexus/syscall.h"

int errno = 0;

void reboot(void) {
    _syscall0(SYS_REBOOT);
    for (;;) {}
}

void klog(const char *msg) {
    _syscall1(SYS_KLOG, (long)msg);
}

int getmemstats(struct mem_stats *ms) {
    long ret = _syscall1(SYS_MEMSTATS, (long)ms);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}
