/*
 * Nexus libc - system wrappers.
 */

#include <unistd.h>
#include <crius/abi.h>
#include "nexus/syscall.h"

void reboot(void) {
    _syscall0(SYS_REBOOT);
    for (;;) {}
}

void klog(const char *msg) {
    _syscall1(SYS_KLOG, (long)msg);
}

int getmemstats(struct mem_stats *ms) {
    return (int)_syscall1(SYS_MEMSTATS, (long)ms);
}
