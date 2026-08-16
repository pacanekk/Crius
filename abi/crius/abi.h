#ifndef CRIUS_ABI_H
#define CRIUS_ABI_H

/*
 * Crius OS — Shared ABI definitions.
 *
 * This header is the SINGLE source of truth for syscall numbers,
 * data layouts, and constants that cross the kernel/userspace boundary.
 *
 * Included by:
 *   - kernel   (#include <crius/abi.h>)
 *   - nexus libc (#include <crius/abi.h>)
 *
 * This file MUST NOT contain:
 *   - kernel-internal types or function declarations
 *   - libc-internal types or function declarations
 *   - inline syscall stubs or any code
 * Only structs, enums, #defines, and typedefs.
 */

#include <stdint.h>
#include <stddef.h>

/* ===== ABI limits ===== */

#define PID_INIT           1
#define MAX_PROCS          16
#define PROC_NAME_LEN      32

#define MAX_DRIVES         4
#define MAX_PARTITIONS     4
#define SECTOR_SIZE        512

#define MAX_ARGS           16
#define MAX_ARG_LEN        255

/* ===== Process states ===== */

enum proc_state {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_SLEEPING,
    PROC_ZOMBIE,
};

/* ===== Process info (returned by SYS_GETPROCINFO) ===== */

struct proc_info {
    int pid;
    enum proc_state state;
    char name[PROC_NAME_LEN];
    uint64_t ticks;
    int priority;
};

/* ===== File types ===== */

#define FILE_TYPE_FILE     1
#define FILE_TYPE_DIR      2
#define FILE_TYPE_DEV      3

/* ===== Block device info (returned by BLK_GET_INFO ioctl) ===== */

struct block_dev_info {
    int present;
    uint64_t sector_count;
    char name[8];
    struct {
        int used;
        uint8_t type;
        uint64_t start_lba;
        uint64_t sector_count;
    } partitions[MAX_PARTITIONS];
};

/* ===== Mount info ===== */

struct mount_info {
    char device[64];
    char path[128];
};

/* ===== Exec context (passed to new process in RDI) ===== */
/*
 * The kernel creates exec_ctx on an argument page mapped at
 * 0x70000000. The process entry point (_start) receives a
 * pointer to this struct in RDI.
 *
 * Layout of the argument page (0x1000 bytes):
 *   [0..31]   struct exec_ctx (argc + argv pointer)
 *   [32..255] char *argv[] array (MAX_ARGS pointers)
 *   [256..]   argument string data (null-terminated)
 *
 * argv pointers are userspace virtual addresses relative to
 * the argument page base (0x70000000).
 */
struct exec_ctx {
    int argc;
    char **argv;
};

/* ===== Ioctl requests ===== */

#define BLK_GET_INFO       1   /* arg = struct block_dev_info * */
#define BLK_READ_SECTOR    2   /* arg = struct blk_io * */
#define BLK_WRITE_SECTOR   3   /* arg = struct blk_io * */

/* ===== Block I/O request (for BLK_READ_SECTOR / BLK_WRITE_SECTOR) ===== */

struct blk_io {
    uint64_t lba;
    size_t count;
    void *buf;
};

/* ===== Syscall numbers ===== */
/*
 * Calling convention: int $0x80
 *   rax = syscall number
 *   rdi, rsi, rdx, r10, r8 = args 1-5
 *   rax = return value
 */

/* Process management */
#define SYS_EXIT            0
#define SYS_KILL            1
#define SYS_SLEEP           2
#define SYS_YIELD           3
#define SYS_SETPRIORITY     4
#define SYS_GETPID          5
#define SYS_GETPROCINFO     6
#define SYS_WAIT            7
#define SYS_EXEC            8
#define SYS_FORK            9

/* Filesystem */
#define SYS_MKDIR           10
#define SYS_UNLINK          11
#define SYS_STAT            12
#define SYS_CHDIR           13
#define SYS_GETCWD          14

/* Mount */
#define SYS_MOUNT           15
#define SYS_UMOUNT          16
#define SYS_MOUNT_COUNT     17
#define SYS_MOUNT_POINT     18
#define SYS_MOUNT_DEVICE    19

/* FD-based I/O */
#define SYS_OPEN            20
#define SYS_CLOSE           21
#define SYS_READ            22
#define SYS_WRITE           23
#define SYS_IOCTL           24

/* System */
#define SYS_REBOOT          25
#define SYS_UPTIME          26
#define SYS_KLOG            27
#define SYS_BOOT_HAS_FB     28
#define SYS_MEMSTATS        29

#define SYS_MAX             30

/* ===== Memory stats (returned by SYS_MEMSTATS) ===== */

struct mem_stats {
    size_t total_pages;
    size_t free_pages;
};

#endif /* CRIUS_ABI_H */
