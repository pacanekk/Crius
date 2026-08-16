/*
 * Nexus libc - process info wrapper.
 */

#include <proc.h>
#include "nexus/syscall.h"

int get_proc_info(pid_t pid, struct proc_info *info) {
    return (int)_syscall2(SYS_GETPROCINFO, (long)pid, (long)info);
}
