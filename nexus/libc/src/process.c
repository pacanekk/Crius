/*
 * Nexus libc - process management wrappers.
 */

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include "nexus/syscall.h"

void exit(int code) {
    _syscall1(SYS_EXIT, (long)code);
    for (;;) { _syscall0(SYS_YIELD); }
}

pid_t fork(void) {
    long ret = _syscall0(SYS_FORK);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (pid_t)ret;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    long ret = _syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

int execv(const char *path, char *const argv[]) {
    return execve(path, argv, NULL);
}

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}

pid_t waitpid(pid_t pid, int *status, int options) {
    long ret = _syscall3(SYS_WAIT, (long)pid, (long)status, (long)options);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (pid_t)ret;
}

int kill(pid_t pid) {
    long ret = _syscall1(SYS_KILL, (long)pid);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}

void sleep(unsigned ticks) {
    _syscall1(SYS_SLEEP, (long)ticks);
}

void yield(void) {
    _syscall0(SYS_YIELD);
}

pid_t getpid(void) {
    long ret = _syscall0(SYS_GETPID);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (pid_t)ret;
}

int setpriority(int pid, int prio) {
    long ret = _syscall2(SYS_SETPRIORITY, (long)pid, (long)prio);
    if (ret < 0) { errno = (int)(-ret); return -1; }
    return (int)ret;
}
