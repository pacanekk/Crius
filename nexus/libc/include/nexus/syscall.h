#ifndef NEXUS_SYSCALL_H
#define NEXUS_SYSCALL_H

/*
 * Nexus libc - private syscall stubs.
 *
 * This header is included ONLY by libc source files and by
 * init.c and security_test.c. It is NOT a public header.
 *
 * Syscall numbers and ABI structs come from <crius/abi.h>.
 */

#include <crius/abi.h>

/* Private syscall stubs - issue int $0x80 to enter kernel. */

static inline long _syscall0(long nr) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline long _syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline long _syscall2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline long _syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline long _syscall4(long nr, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline long _syscall5(long nr, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

#endif /* NEXUS_SYSCALL_H */
