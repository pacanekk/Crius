/*
 * Nexus libc - process info wrapper.
 */

#include <errno.h>
#include <proc.h>
#include "nexus/syscall.h"

int get_proc_info(pid_t pid, struct proc_info *info) {
    long ret = _syscall2(SYS_GETPROCINFO, (long)pid, (long)info);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}
