/*
 * Nexus libc - process management wrappers.
 */

#include <unistd.h>
#include <sys/types.h>
#include "nexus/syscall.h"

void exit(int code) {
    _syscall1(SYS_EXIT, (long)code);
    for (;;) { _syscall0(SYS_YIELD); }
}

pid_t fork(void) {
    return (pid_t)_syscall0(SYS_FORK);
}

int exec(const char *path, int argc, char **argv) {
    return (int)_syscall3(SYS_EXEC, (long)path, (long)argc, (long)argv);
}

pid_t wait(pid_t pid) {
    return (pid_t)_syscall1(SYS_WAIT, (long)pid);
}

int kill(pid_t pid) {
    return (int)_syscall1(SYS_KILL, (long)pid);
}

void sleep(unsigned ticks) {
    _syscall1(SYS_SLEEP, (long)ticks);
}

void yield(void) {
    _syscall0(SYS_YIELD);
}

pid_t getpid(void) {
    return (pid_t)_syscall0(SYS_GETPID);
}

int setpriority(int pid, int prio) {
    return (int)_syscall2(SYS_SETPRIORITY, (long)pid, (long)prio);
}
