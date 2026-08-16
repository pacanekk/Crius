/*
 * process/exec.c - exec() implementation.
 *
 * Replaces the current process image with a new ELF binary.
 * The old kernel stack is NOT freed here because the syscall frame
 * (syscall_saved_rsp) still lives on it.  The old stack pointer is
 * saved in the task's pending_free_kstack field and freed by the
 * scheduler after the context switch completes.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <crius/abi.h>
#include "process/scheduler.h"
#include "process/sched_internal.h"
#include "process/elf_exec.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "arch/gdt.h"

#define USER_STACK_TOP   0x80000000UL
#define USER_STACK_PAGES 32
#define ARG_PAGE_BASE    0x70000000UL

int do_exec(const char *path, int argc, char **argv)
{
    if (!path || argc < 0 || argc > MAX_ARGS)
        return -1;

    /* 1. Stat the file */
    int type;
    size_t elf_size;
    if (vfs_stat(path, &type, &elf_size) < 0)
        return -1;

    if (elf_size > 16 * 1024 * 1024)
        return -1;

    /* 2. Read ELF into kernel buffer */
    void *elf_buf = kmalloc(elf_size);
    if (!elf_buf)
        return -1;
    int n = vfs_read(path, elf_buf, elf_size);
    if (n != (int)elf_size) {
        kfree(elf_buf);
        return -1;
    }

    /* 3. Create new address space */
    uint64_t new_cr3 = vmm_create_pml4();
    if (!new_cr3) {
        kfree(elf_buf);
        return -1;
    }
    vmm_map_kernel(new_cr3);

    uint64_t entry = elf_load_to_task(new_cr3, elf_buf, elf_size);
    kfree(elf_buf);
    if (entry == 0) {
        vmm_free_user_pages(new_cr3);
        pmm_free_page(new_cr3);
        return -1;
    }

    /* 4. Set up argument page */
    uint64_t arg_phys = pmm_alloc_page();
    if (arg_phys == 0) {
        vmm_free_user_pages(new_cr3);
        pmm_free_page(new_cr3);
        return -1;
    }
    uint8_t *arg_kptr = (uint8_t *)(elf_exec_get_hhdm() + arg_phys);
    memset(arg_kptr, 0, 0x1000);

    struct exec_ctx *ctx = (struct exec_ctx *)arg_kptr;
    ctx->argc = argc;
    ctx->argv = (char **)(ARG_PAGE_BASE + 32);

    uint32_t str_off = 256;
    char **argv_ptrs = (char **)(arg_kptr + 32);
    for (int i = 0; i < argc && i < MAX_ARGS; i++) {
        if (!argv[i]) continue;
        argv_ptrs[i] = (char *)(ARG_PAGE_BASE + str_off);
        int j;
        for (j = 0; argv[i][j] && str_off < 0x1000 - 1; j++, str_off++)
            arg_kptr[str_off] = argv[i][j];
        arg_kptr[str_off++] = '\0';
    }

    if (vmm_map_page(new_cr3, ARG_PAGE_BASE, arg_phys,
                 PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NX) < 0) {
        pmm_free_page(arg_phys);
        vmm_free_user_pages(new_cr3);
        pmm_free_page(new_cr3);
        return -1;
    }

    /* 5. Map user stack */
    for (int p = 0; p < USER_STACK_PAGES; p++) {
        uint64_t usp = pmm_alloc_page();
        if (usp == 0) {
            /* Rollback: vmm_free_user_pages frees all user-mapped data pages
             * (ELF pages, arg page, and any stack pages already mapped)
             * and their page tables.  Then free the PML4 itself. */
            vmm_free_user_pages(new_cr3);
            pmm_free_page(new_cr3);
            return -1;
        }
        if (vmm_map_page(new_cr3, USER_STACK_TOP - (p + 1) * 0x1000, usp,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NX) < 0) {
            pmm_free_page(usp);
            vmm_free_user_pages(new_cr3);
            pmm_free_page(new_cr3);
            return -1;
        }
    }

    /* 6. Get current task */
    struct task *t = task_current();
    if (!t) {
        vmm_free_user_pages(new_cr3);
        pmm_free_page(new_cr3);
        return -1;
    }

    /* 7. Allocate NEW kernel stack - do NOT free old one yet */
    void *new_kstack = kmalloc(TASK_STACK_SIZE);
    if (!new_kstack) {
        vmm_free_user_pages(new_cr3);
        pmm_free_page(new_cr3);
        return -1;
    }

    /* 8. Save old CR3 and old kernel stack for deferred cleanup */
    uint64_t old_cr3 = t->cr3;
    void *old_kstack = t->kernel_stack;

    /* 9. Extract basename for task name */
    char path_buf[128];
    int pi;
    for (pi = 0; path[pi] && pi < 127; pi++) path_buf[pi] = path[pi];
    path_buf[pi] = '\0';

    const char *name = path_buf;
    for (const char *s = path_buf; *s; s++)
        if (*s == '/') name = s + 1;

    /* 10. Update task fields */
    t->kernel_stack = new_kstack;
    t->stack = new_kstack;
    t->cr3 = new_cr3;
    t->rip = entry;
    t->user_rsp = USER_STACK_TOP;

    memset(t->name, 0, TASK_NAME_LEN);
    for (int i = 0; name[i] && i < TASK_NAME_LEN - 1; i++)
        t->name[i] = name[i];

    /* 11. Build new kernel stack frame (irq_common layout) */
    uint64_t kstack_top = (uint64_t)t->kernel_stack + TASK_STACK_SIZE;
    uint64_t *sp = (uint64_t *)kstack_top;

    /* iretq frame */
    *--sp = 0x1B;               /* SS = user data | RPL 3 */
    *--sp = USER_STACK_TOP - 16;/* RSP = user stack (16-byte aligned) */
    *--sp = 0x202;              /* RFLAGS = IF set */
    *--sp = 0x23;               /* CS = user code | RPL 3 */
    *--sp = entry;              /* RIP = entry point */

    /* stub pushes */
    *--sp = 0;                  /* error_code */
    *--sp = 0x20;               /* vector (timer) */

    /* 15 registers (irq_common order) */
    for (int i = 0; i < 15; i++)
        *--sp = 0;
    sp[9] = ARG_PAGE_BASE;      /* RDI = arg pointer */

    t->rsp = (uint64_t)sp;

    /* 12. Switch to new address space */
    vmm_switch_pml4(new_cr3);

    /* 13. Free old user pages (old CR3) - safe, we're in new CR3 now */
    vmm_free_user_pages(old_cr3);
    pmm_free_page(old_cr3);

    /* 14. Set TSS rsp0 to new kernel stack */
    tss_set_rsp0((uint64_t)t->kernel_stack + TASK_STACK_SIZE);

    /*
     * 15. Defer old kernel stack free.
     * The syscall frame (syscall_saved_rsp) still points into old_kstack.
     * We save it so the scheduler can free it after the next context switch
     * loads the new RSP.
     *
     * If a previous exec left a pending stack that was never freed by the
     * scheduler (e.g. double exec without context switch), free it now -
     * it is no longer in use.
     */
    if (t->pending_free_kstack) {
        kfree(t->pending_free_kstack);
        t->pending_free_kstack = NULL;
    }
    t->pending_free_kstack = old_kstack;

    /*
     * 16. Modify the syscall return frame so that iretq jumps to the
     * new entry point instead of returning to the old user code.
     * Unified frame layout (same as irq_common):
     *   [0]=r15 [1]=r14 ... [14]=rax
     *   [15]=vector [16]=error_code
     *   [17]=RIP [18]=CS [19]=RFLAGS [20]=RSP [21]=SS
     */
    uint64_t *sf = (uint64_t *)syscall_saved_rsp;
    sf[17] = entry;
    sf[18] = 0x23;
    sf[19] = 0x202;
    sf[20] = USER_STACK_TOP - 16;
    sf[21] = 0x1B;
    sf[14] = 0;              /* rax = 0 (success) */
    sf[9]  = ARG_PAGE_BASE;  /* rdi = arg pointer */

    return 0;
}
