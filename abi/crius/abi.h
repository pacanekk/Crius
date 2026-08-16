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

/* ===== Open flags (used by SYS_OPEN) ===== */

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     4
#define O_APPEND    8
#define O_TRUNC     16
#define O_EXCL      32
#define O_NOCTTY    64
#define O_NONBLOCK  128
#define O_CLOEXEC   256
#define O_SYNC      512

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

/* ===== waitpid status values ===== */
#define WNOHANG   1
#define WUNTRACED 2

#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WTERMSIG(s)    ((s) & 0x7F)
#define WIFEXITED(s)   (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (WTERMSIG(s) > 0 && WTERMSIG(s) != 0x7F)

/* ===== minimal signal numbers ===== */
#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGILL  4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS  7
#define SIGFPE  8
#define SIGKILL 9
#define SIGSEGV 11
#define SIGTERM 15

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

/* ===== Error numbers (returned as negative values by the kernel) ===== */

#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define ENOTBLK      15
#define EBUSY        16
#define EEXIST       17
#define ENOTDIR      18
#define EISDIR       19
#define ENODEV       20
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define ENOTTY       25
#define EFBIG        27
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define EMLINK       31
#define EPIPE        32
#define EDOM         33
#define ERANGE       34
#define ENOSYS       35
#define ENOTEMPTY    39

/* ===== struct stat ===== */

struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

/* ===== File type and permission bits ===== */

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

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
