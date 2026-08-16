/*
 * process/fork.c - fork() implementation.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "process/scheduler.h"
#include "process/sched_internal.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

int task_fork(uint64_t saved_rsp_local) {
    struct task *parent = task_current();
    if (!parent) return -1;

    int slot = alloc_task();
    if (slot < 0) return -1;

    struct task *child = &tasks[slot];

    memset(child->name, 0, TASK_NAME_LEN);
    for (int i = 0; parent->name[i] && i < TASK_NAME_LEN - 1; i++)
        child->name[i] = parent->name[i];

    /* Allocate child's own kernel stack */
    child->stack = kmalloc(TASK_STACK_SIZE);
    if (!child->stack) return -1;
    child->kernel_stack = child->stack;

    child->cr3 = vmm_create_pml4();
    if (!child->cr3) {
        kfree(child->stack);
        child->stack = NULL;
        child->kernel_stack = NULL;
        return -1;
    }
    vmm_map_kernel(child->cr3);
    if (vmm_copy_userspace(child->cr3, parent->cr3) < 0) {
        vmm_free_user_pages(child->cr3);
        pmm_free_page(child->cr3);
        child->cr3 = 0;
        kfree(child->stack);
        child->stack = NULL;
        child->kernel_stack = NULL;
        child->state = TASK_UNUSED;
        return -1;
    }
    /* Flush TLB so parent's COW-marked (read-only) PTEs take effect.
     * Without this, stale TLB entries let the parent write to shared
     * pages without triggering COW faults, corrupting the child's data. */
    vmm_switch_pml4(parent->cr3);

    child->user_rsp = parent->user_rsp;

    /* Build child kernel stack frame to match irq_common/syscall_isr layout:
     * Both use the same layout now:
     *   [0..112]  = 15 registers (r15 at [0], rax at [14])
     *   [120..128] = vector + error code
     *   [136..176] = RIP, CS, RFLAGS, RSP, SS
     * Total = 120 + 16 + 40 = 176 bytes */
    uint64_t child_stack_top = (uint64_t)child->stack + TASK_STACK_SIZE;
    uint64_t *child_frame = (uint64_t *)(child_stack_top - 176);

    /* Copy 15 registers - same order in both frames now */
    uint64_t *src_regs = (uint64_t *)saved_rsp_local;
    for (int i = 0; i < 15; i++) {
        child_frame[i] = src_regs[i];
    }

    /* Insert fake vector + error code */
    child_frame[15] = 0x80; /* syscall vector */
    child_frame[16] = 0;    /* error code */

    /* Copy CPU frame: RIP, CS, RFLAGS, RSP, SS - at [17..21] in both layouts */
    memcpy(&child_frame[17], &src_regs[17], 40);

    /* Child's RSP is the actual user RSP from the syscall frame */
    child_frame[20] = src_regs[20];

    /* Child returns 0 from fork - rax is at [14] in both layouts */
    child_frame[14] = 0;

    child->rsp = (uint64_t)child_frame;
    child->rip = child_frame[17];
    child->ticks = 0;
    child->priority = parent->priority;
    child->sleep_until = 0;
    child->arg = parent->arg;
    child->state = TASK_READY;
    child->parent_pid = task_current_id();
    child->first_child = -1;
    child->exit_code = 0;
    child->wait_for_pid = -1;
    child->pending_free_kstack = NULL;
    memcpy(child->cwd, parent->cwd, 128);

    /* Share file descriptors with refcount */
    for (int i = 0; i < MAX_FDS; i++) {
        if (parent->fds[i]) {
            file_get(parent->fds[i]);
            child->fds[i] = parent->fds[i];
        } else {
            child->fds[i] = NULL;
        }
    }

    /* Insert child into parent's child list */
    child->next_sibling = parent->first_child;
    parent->first_child = slot;

    return slot;
}
