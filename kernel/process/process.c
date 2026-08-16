/*
 * process/process.c - process lifecycle management.
 *
 * create, cleanup, exit, wait, kill, sleep, reparenting.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "process/scheduler.h"
#include "process/sched_internal.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "drivers/serial.h"

#define USER_STACK_TOP   0x80000000UL
#define USER_STACK_PAGES 32

struct task *task_current(void) {
    if (current_task < 0) return NULL;
    return &tasks[current_task];
}

struct task *task_get(int i) {
    if (i < 0 || i >= MAX_TASKS) return NULL;
    return &tasks[i];
}

int task_current_id(void) {
    return current_task;
}

int task_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_ZOMBIE)
            count++;
    return count;
}

int alloc_task(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED)
            return i;
    }
    return -1;
}

uint64_t task_get_cr3(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    if (tasks[id].state == TASK_UNUSED) return 0;
    return tasks[id].cr3;
}

static int init_task_stack(int slot, const char *name, uint64_t entry, void *arg) {
    struct task *t = &tasks[slot];
    memset(t->name, 0, TASK_NAME_LEN);
    for (int i = 0; name[i] && i < TASK_NAME_LEN - 1; i++)
        t->name[i] = name[i];

    /* Allocate kernel stack */
    t->stack = kmalloc(TASK_STACK_SIZE);
    if (!t->stack) return -1;
    t->kernel_stack = t->stack;

    t->cr3 = vmm_create_pml4();
    if (!t->cr3) {
        kfree(t->stack);
        t->stack = NULL;
        t->kernel_stack = NULL;
        return -1;
    }
    vmm_map_kernel(t->cr3);

    /* Allocate user stack - USER_STACK_PAGES pages below USER_STACK_TOP */
    for (int p = 0; p < USER_STACK_PAGES; p++) {
        uint64_t page_phys = pmm_alloc_page();
        if (page_phys == 0) {
            /* Rollback: vmm_free_user_pages frees all user-mapped data pages
             * and their page tables.  Then free the PML4 itself. */
            vmm_free_user_pages(t->cr3);
            pmm_free_page(t->cr3);
            t->cr3 = 0;
            kfree(t->stack);
            t->stack = NULL;
            t->kernel_stack = NULL;
            return -1;
        }
        if (vmm_map_page(t->cr3, USER_STACK_TOP - (p + 1) * 0x1000, page_phys,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NX) < 0) {
            pmm_free_page(page_phys);
            vmm_free_user_pages(t->cr3);
            pmm_free_page(t->cr3);
            t->cr3 = 0;
            kfree(t->stack);
            t->stack = NULL;
            t->kernel_stack = NULL;
            return -1;
        }
    }
    t->user_rsp = USER_STACK_TOP;

    /* Build stack frame for irq_common resume:
     * Layout (low to high): 15 regs, vector, error_code, RIP, CS, RFLAGS, RSP, SS
     * irq_common pops 15 regs, adds 16 (skip vector+error_code), iretq pops 5 */
    uint64_t stack_top = (uint64_t)t->stack + TASK_STACK_SIZE;
    uint64_t *sp = (uint64_t *)stack_top;

    /* iretq frame */
    *--sp = 0x1B;              /* SS = user data | RPL 3 */
    *--sp = USER_STACK_TOP;    /* RSP = user stack top */
    *--sp = 0x202;             /* RFLAGS = IF set */
    *--sp = 0x23;              /* CS = user code | RPL 3 */
    *--sp = entry;             /* RIP = user entry point */

    /* stub pushes */
    *--sp = 0;                 /* error_code */
    *--sp = 0;                 /* vector */

    /* 15 registers (irq_common order: rax first pushed = highest addr) */
    for (int i = 0; i < 15; i++)
        *--sp = 0;

    /* Set RDI = arg (irq_common: [0]=r15, [1]=r14, ..., [9]=rdi) */
    sp[9] = (uint64_t)arg;

    t->rsp = (uint64_t)sp;
    t->rip = entry;
    t->ticks = 0;
    t->priority = TASK_DEFAULT_PRIORITY;
    t->sleep_until = 0;
    t->arg = arg;
    t->state = TASK_READY;
    t->parent_pid = (current_task >= 0) ? current_task : -1;
    t->first_child = -1;
    t->exit_code = 0;
    t->wait_for_pid = -1;
    t->next_sibling = -1;
    t->pending_free_kstack = NULL;
    t->cwd[0] = '/';
    t->cwd[1] = '\0';

    /* FD table starts empty - boot/init code sets up stdin/stdout/stderr */
    memset(t->fds, 0, sizeof(t->fds));

    return 0;
}

int task_create(const char *name, void (*entry)(void)) {
    int slot = alloc_task();
    if (slot < 0) return -1;
    if (init_task_stack(slot, name, (uint64_t)entry, NULL) < 0) return -1;

    if (current_task < 0) {
        current_task = slot;
        tasks[slot].state = TASK_RUNNING;
    }

    return slot;
}

int task_create_args(const char *name, void (*entry)(void *), void *arg) {
    int slot = alloc_task();
    if (slot < 0) return -1;
    if (init_task_stack(slot, name, (uint64_t)entry, arg) < 0) return -1;

    if (current_task < 0) {
        current_task = slot;
        tasks[slot].state = TASK_RUNNING;
    }

    return slot;
}

int task_create_current(const char *name) {
    int slot = 0;
    struct task *t = &tasks[slot];
    memset(t->name, 0, TASK_NAME_LEN);
    for (int i = 0; name[i] && i < TASK_NAME_LEN - 1; i++)
        t->name[i] = name[i];

    /* Idle task gets its own kernel stack so interrupts don't triple-fault */
    t->stack = kmalloc(TASK_STACK_SIZE);
    if (!t->stack) return -1;
    t->kernel_stack = t->stack;

    t->user_rsp = 0;
    t->rsp = 0;
    t->rip = 0;
    t->cr3 = vmm_current_pml4();
    t->ticks = 0;
    t->priority = TASK_DEFAULT_PRIORITY;
    t->sleep_until = 0;
    t->arg = NULL;
    t->state = TASK_RUNNING;
    t->parent_pid = -1;
    t->first_child = -1;
    t->exit_code = 0;
    t->wait_for_pid = -1;
    t->next_sibling = -1;
    t->pending_free_kstack = NULL;
    t->cwd[0] = '/';
    t->cwd[1] = '\0';
    memset(t->fds, 0, sizeof(t->fds));
    current_task = slot;
    return slot;
}

static void task_release_fds(struct task *t) {
    if (!t) return;
    for (int i = 0; i < MAX_FDS; i++) {
        if (t->fds[i]) {
            file_put(t->fds[i]);
            t->fds[i] = NULL;
        }
    }
}

void task_cleanup(struct task *t) {
    if (!t) return;

    /* Free user address space - but NEVER the currently active CR3 */
    if (t->cr3) {
        uint64_t active_cr3 = vmm_current_pml4();
        if (t->cr3 != active_cr3) {
            vmm_free_user_pages(t->cr3);
            pmm_free_page(t->cr3);
        }
        t->cr3 = 0;
    }

    /* Free kernel stack - stack and kernel_stack are typically the same pointer */
    if (t->kernel_stack) {
        kfree(t->kernel_stack);
        t->kernel_stack = NULL;
    }
    if (t->stack && t->stack != t->kernel_stack) {
        kfree(t->stack);
    }
    t->stack = NULL;

    /* Free deferred old kernel stack from exec (if not already freed) */
    if (t->pending_free_kstack) {
        kfree(t->pending_free_kstack);
        t->pending_free_kstack = NULL;
    }

    /* Release file descriptors */
    task_release_fds(t);
}

void task_reparent_children(int dying_pid) {
    int new_parent = 1;
    if (tasks[1].state == TASK_UNUSED || tasks[1].state == TASK_ZOMBIE)
        new_parent = 0;
    /* Never reparent to the dying process itself */
    if (new_parent == dying_pid)
        new_parent = 0;

    int child = tasks[dying_pid].first_child;
    while (child >= 0) {
        int next = tasks[child].next_sibling;
        tasks[child].parent_pid = new_parent;
        tasks[child].next_sibling = tasks[new_parent].first_child;
        tasks[new_parent].first_child = child;
        child = next;
    }
    tasks[dying_pid].first_child = -1;
}

static void task_remove_child(int parent_pid, int child_pid) {
    if (tasks[parent_pid].first_child == child_pid) {
        tasks[parent_pid].first_child = tasks[child_pid].next_sibling;
    } else {
        int prev = tasks[parent_pid].first_child;
        while (prev >= 0 && tasks[prev].next_sibling != child_pid)
            prev = tasks[prev].next_sibling;
        if (prev >= 0)
            tasks[prev].next_sibling = tasks[child_pid].next_sibling;
    }
    tasks[child_pid].next_sibling = -1;
}

void task_exit_code(int code) {
    if (current_task >= 0) {
        int dying_pid = current_task;
        task_reparent_children(dying_pid);

        /* Release FDs before becoming zombie - zombie should hold no resources */
        task_release_fds(&tasks[dying_pid]);

        /* Free deferred old kernel stack from exec (if any) */
        if (tasks[dying_pid].pending_free_kstack) {
            kfree(tasks[dying_pid].pending_free_kstack);
            tasks[dying_pid].pending_free_kstack = NULL;
        }

        tasks[dying_pid].state = TASK_ZOMBIE;
        tasks[dying_pid].exit_code = code;
    }
    need_reschedule = 1;
    /* We never return to syscall_isr, so it can't check need_reschedule.
     * Enable interrupts so the timer fires, irq_common calls do_schedule,
     * and the parent task is woken to reap this zombie. */
    for (;;) __asm__ volatile ("sti\n\thlt" ::: "memory");
}

int task_kill(int id) {
    if (id < 0 || id >= MAX_TASKS) return -ESRCH;
    if (id == current_task) return -EINVAL;
    if (id == 0) return -EPERM;  /* cannot kill idle task */
    if (tasks[id].state == TASK_UNUSED || tasks[id].state == TASK_ZOMBIE)
        return -ESRCH;

    task_reparent_children(id);

    /* Release FDs before becoming zombie */
    task_release_fds(&tasks[id]);

    /* Free deferred old kernel stack from exec (if any) */
    if (tasks[id].pending_free_kstack) {
        kfree(tasks[id].pending_free_kstack);
        tasks[id].pending_free_kstack = NULL;
    }

    tasks[id].state = TASK_ZOMBIE;
    tasks[id].exit_code = -SIGKILL;
    return 0;
}

int task_wait(int pid, int *status, int options) {
    int parent = current_task;
    if (parent < 0) return -ECHILD;

    for (;;) {
        int found = -1;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state != TASK_ZOMBIE) continue;
            if (tasks[i].parent_pid != parent) continue;
            if (pid != -1 && i != pid) continue;
            found = i;
            break;
        }

        if (found >= 0) {
            int code = tasks[found].exit_code;
            int st = 0;
            if (code < 0) {
                st = -code; /* signal number */
            } else {
                st = (code & 0xFF) << 8;
            }
            if (status) *status = st;
            task_remove_child(parent, found);
            task_cleanup(&tasks[found]);
            tasks[found].state = TASK_UNUSED;
            tasks[found].exit_code = 0;
            tasks[found].parent_pid = -1;
            tasks[found].wait_for_pid = -1;
            tasks[found].next_sibling = -1;
            tasks[found].first_child = -1;
            tasks[found].pending_free_kstack = NULL;
            return found;
        }

        /* Check that a matching child still exists */
        int has_child = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_ZOMBIE)
                continue;
            if (tasks[i].parent_pid != parent) continue;
            if (pid != -1 && i != pid) continue;
            has_child = 1;
            break;
        }
        if (!has_child)
            return -ECHILD;

        if (options & WNOHANG)
            return 0;

        /* Block until child exits. */
        tasks[parent].state = TASK_BLOCKED;
        tasks[parent].wait_for_pid = pid;
        need_reschedule = 1;
        __asm__ volatile ("sti\n\thlt" ::: "memory");
    }
}

void task_sleep(uint64_t ms) {
    if (current_task < 0) return;
    tasks[current_task].sleep_until = scheduler_ticks_total + ms;
    tasks[current_task].state = TASK_SLEEPING;
    need_reschedule = 1;
    /* Block until scheduler wakes us (sets state to READY) */
    while (tasks[current_task].state == TASK_SLEEPING) {
        __asm__ volatile ("sti\n\thlt" ::: "memory");
    }
}

void task_set_priority(int id, int priority) {
    if (id < 0 || id >= MAX_TASKS) return;
    if (priority < 0) priority = 0;
    if (priority > TASK_MAX_PRIORITY) priority = TASK_MAX_PRIORITY;
    tasks[id].priority = priority;
}
