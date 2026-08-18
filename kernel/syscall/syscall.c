#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <crius/abi.h>
#include "process/scheduler.h"
#include "fs/vfs.h"
#include "drivers/framebuffer.h"
#include "drivers/serial.h"
#include "arch/io.h"
#include "mm/kmalloc.h"
#include "process/elf_exec.h"
#include "process/exec.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "arch/gdt.h"
#include "syscall/usercopy.h"
#include "fs/file.h"

uint64_t syscall_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    switch (nr) {
    /* ===== Process management ===== */
    case SYS_EXIT: task_exit_code((int)a1); return 0;
    case SYS_KILL: return (uint64_t)task_kill((int)a1);
    case SYS_SLEEP: task_sleep(a1); return 0;
    case SYS_YIELD: scheduler_yield(); return 0;
    case SYS_SETPRIORITY: task_set_priority((int)a1, (int)a2); return 0;
    case SYS_GETPID: return (uint64_t)task_current_id();
    case SYS_GETPROCINFO: {
        if (!validate_user_ptr_writable((void *)a2, sizeof(struct proc_info))) return (uint64_t)-1;
        struct proc_info *info = (struct proc_info *)a2;
        struct task *t = task_get((int)a1);
        if (!t || t->state == TASK_UNUSED || t->state == TASK_ZOMBIE) return (uint64_t)-1;
        info->pid = (int)a1;
        info->state = (enum proc_state)t->state;
        info->ticks = t->ticks;
        info->priority = t->priority;
        memset(info->name, 0, PROC_NAME_LEN);
        for (int i = 0; t->name[i] && i < PROC_NAME_LEN - 1; i++)
            info->name[i] = t->name[i];
        return 0;
    }
    case SYS_WAIT: {
        int pid = (int)a1;
        int options = (int)a3;
        int status;
        int ret = task_wait(pid, &status, options);
        if (ret < 0) return (uint64_t)ret;
        if (a2) {
            if (!validate_user_ptr_writable((void *)a2, sizeof(int)))
                return (uint64_t)-EFAULT;
            int *ustatus = (int *)a2;
            *ustatus = status;
        }
        return (uint64_t)ret;
    }
    case SYS_EXEC: {
        const char *path = (const char *)a1;
        char **argv = (char **)a2;
        char **envp = (char **)a3;

        if (!validate_user_string(path, 255)) return (uint64_t)-EFAULT;

        if (!argv) return (uint64_t)-EFAULT;
        int i;
        for (i = 0; i < MAX_ARGS; i++) {
            if (!validate_user_ptr((const void *)(argv + i), sizeof(char *)))
                return (uint64_t)-EFAULT;
            if (!argv[i]) break;
            if (!validate_user_string(argv[i], MAX_ARG_LEN))
                return (uint64_t)-EFAULT;
        }
        if (i == MAX_ARGS) {
            if (!validate_user_ptr((const void *)(argv + MAX_ARGS), sizeof(char *)))
                return (uint64_t)-EFAULT;
            if (argv[MAX_ARGS]) return (uint64_t)-E2BIG;
        }

        if (envp) {
            if (!validate_user_ptr((const void *)envp, sizeof(char *)))
                return (uint64_t)-EFAULT;
        }

        return (uint64_t)do_exec(path, argv, envp);
    }
    case SYS_FORK: {
        uint64_t fork_saved_rsp = syscall_saved_rsp;
        int result = task_fork(fork_saved_rsp);
        return (uint64_t)result;
    }

    /* ===== Filesystem ===== */
    case SYS_MKDIR: {
        if (!validate_user_string((const char *)a1, 255)) return (uint64_t)-1;
        return (uint64_t)vfs_mkdir((const char *)a1);
    }
    case SYS_UNLINK: {
        if (!validate_user_string((const char *)a1, 255)) return (uint64_t)-1;
        return (uint64_t)vfs_delete((const char *)a1);
    }
    case SYS_STAT: {
        if (!validate_user_string((const char *)a1, 255)) return (uint64_t)-EFAULT;
        if (!validate_user_ptr_writable((void *)a2, sizeof(struct stat))) return (uint64_t)-EFAULT;
        return (uint64_t)vfs_stat((const char *)a1, (struct stat *)a2);
    }
    case SYS_CHDIR: {
        if (!validate_user_string((const char *)a1, 255)) return (uint64_t)-1;
        return (uint64_t)vfs_chdir((const char *)a1);
    }
    case SYS_GETCWD: {
        if (!validate_user_ptr_writable((void *)a1, (size_t)a2)) return (uint64_t)-EFAULT;
        return (uint64_t)vfs_pwd((char *)a1, (int)a2);
    }

    /* ===== Mount ===== */
    case SYS_MOUNT: {
        if (!validate_user_string((const char *)a1, 63)) return (uint64_t)-1;
        if (!validate_user_string((const char *)a2, 127)) return (uint64_t)-1;
        return (uint64_t)vfs_mount((const char *)a1, (const char *)a2);
    }
    case SYS_UMOUNT: {
        if (!validate_user_string((const char *)a1, 127)) return (uint64_t)-1;
        return (uint64_t)vfs_umount((const char *)a1);
    }
    case SYS_MOUNT_COUNT: return (uint64_t)vfs_mount_count();
    case SYS_MOUNT_POINT: return (uint64_t)vfs_mount_point((int)a1);
    case SYS_MOUNT_DEVICE: return (uint64_t)vfs_mount_device((int)a1);

    /* ===== FD-based I/O ===== */
    case SYS_OPEN: {
        const char *path = (const char *)a1;
        int flags = (int)a2;
        if (!validate_user_string(path, 127)) return (uint64_t)-1;
        struct task *t = task_current();
        if (!t) return (uint64_t)-1;

        char abs[256];
        vfs_resolve_abs(path, abs, sizeof(abs));

        struct file *f = vfs_open(abs, flags);
        if (!f) return (uint64_t)-1;

        for (int fd = 3; fd < MAX_FDS; fd++) {
            if (!t->fds[fd]) {
                t->fds[fd] = f;
                return (uint64_t)fd;
            }
        }
        file_put(f);
        return (uint64_t)-1;
    }
    case SYS_CLOSE: {
        int fd = (int)a1;
        struct task *t = task_current();
        if (!t || fd < 0 || fd >= MAX_FDS) return (uint64_t)-1;
        if (!t->fds[fd]) return (uint64_t)-1;
        file_put(t->fds[fd]);
        t->fds[fd] = NULL;
        return 0;
    }
    case SYS_READ: {
        int fd = (int)a1;
        char *buf = (char *)a2;
        int count = (int)a3;
        struct task *t = task_current();
        if (!t || fd < 0 || fd >= MAX_FDS || !t->fds[fd]) return (uint64_t)-1;
        if (!buf || count <= 0) return (uint64_t)-1;
        if (!validate_user_ptr_writable(buf, (size_t)count)) return (uint64_t)-1;
        return (uint64_t)file_read(t->fds[fd], buf, count);
    }
    case SYS_WRITE: {
        int fd = (int)a1;
        const char *buf = (const char *)a2;
        int count = (int)a3;
        struct task *t = task_current();
        if (!t || fd < 0 || fd >= MAX_FDS || !t->fds[fd]) return (uint64_t)-1;
        if (!buf || count <= 0) return (uint64_t)-1;
        if (!validate_user_ptr(buf, (size_t)count)) return (uint64_t)-1;
        return (uint64_t)file_write(t->fds[fd], buf, count);
    }
    case SYS_IOCTL: {
        int fd = (int)a1;
        unsigned long request = (unsigned long)a2;
        void *arg = (void *)a3;
        struct task *t = task_current();
        if (!t || fd < 0 || fd >= MAX_FDS || !t->fds[fd]) return (uint64_t)-1;
        if (request == BLK_GET_INFO) {
            if (!validate_user_ptr_writable(arg, sizeof(struct block_dev_info))) return (uint64_t)-1;
        } else if (request == BLK_READ_SECTOR || request == BLK_WRITE_SECTOR) {
            if (!validate_user_ptr_writable(arg, sizeof(struct blk_io))) return (uint64_t)-1;
            struct blk_io *io = (struct blk_io *)arg;
            if (request == BLK_READ_SECTOR) {
                if (!validate_user_ptr_writable(io->buf, io->count * SECTOR_SIZE)) return (uint64_t)-1;
            } else {
                if (!validate_user_ptr(io->buf, io->count * SECTOR_SIZE)) return (uint64_t)-1;
            }
        } else {
            return (uint64_t)-1;
        }
        return (uint64_t)file_ioctl(t->fds[fd], request, arg);
    }

    /* ===== System ===== */
    case SYS_REBOOT: {
        outb(0x64, 0xFE);
        return 0;
    }
    case SYS_UPTIME: {
        extern volatile uint64_t scheduler_ticks_total;
        return scheduler_ticks_total;
    }
    case SYS_KLOG: {
        if (!validate_user_string((const char *)a1, 4096)) return (uint64_t)-1;
        serial_puts((const char *)a1); return 0;
    }
    case SYS_BOOT_HAS_FB: return (uint64_t)fb_is_available();
    case SYS_MEMSTATS: {
        if (!validate_user_ptr_writable((void *)a1, sizeof(struct mem_stats)))
            return (uint64_t)-1;
        struct mem_stats *ms = (struct mem_stats *)a1;
        pmm_stats(&ms->total_pages, &ms->free_pages);
        return 0;
    }

    default:
        serial_puts("syscall: unknown nr=\n");
        return (uint64_t)-1;
    }
}
