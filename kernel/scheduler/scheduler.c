#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "process/scheduler.h"
#include "process/sched_internal.h"
#include "arch/apic.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "arch/gdt.h"
#include "drivers/serial.h"

struct task tasks[MAX_TASKS];
int current_task = -1;
static volatile uint64_t timer_ticks = 0;
int sched_initialized = 0;

volatile int need_reschedule = 0;
volatile uint64_t saved_rsp = 0;
volatile uint64_t syscall_saved_rsp = 0;
volatile uint64_t scheduler_ticks_total = 0;
volatile uint64_t sched_stack_top = 0;

void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_UNUSED;
        tasks[i].priority = TASK_DEFAULT_PRIORITY;
        tasks[i].sleep_until = 0;
        tasks[i].arg = NULL;
        tasks[i].parent_pid = -1;
        tasks[i].first_child = -1;
        tasks[i].exit_code = 0;
        tasks[i].wait_for_pid = -1;
        tasks[i].next_sibling = -1;
        tasks[i].pending_free_kstack = NULL;
    }
    current_task = -1;
    timer_ticks = 0;

    /* Allocate dedicated scheduler stack (16 KiB) */
    void *sstack = kmalloc(16384);
    if (sstack) {
        sched_stack_top = (uint64_t)sstack + 16384;
    }

    sched_initialized = 1;
}

void do_schedule(void) {
    int prev = current_task;

    if (prev >= 0 && tasks[prev].state == TASK_RUNNING)
        tasks[prev].state = TASK_READY;

    if (prev >= 0) {
        if (tasks[prev].pending_free_kstack) {
            /* exec left an old kernel stack pending free. Check whether
             * saved_rsp is still on the OLD kernel stack (we're inside
             * the exec syscall) or on the NEW kernel stack (exec already
             * returned to userspace and we got a timer interrupt).
             * Only skip saving when we're on the old stack - otherwise
             * we'd lose the current interrupt frame and restart the
             * program from the exec entry point. */
            uint64_t old_base = (uint64_t)tasks[prev].pending_free_kstack;
            if (saved_rsp < old_base || saved_rsp >= old_base + TASK_STACK_SIZE)
                tasks[prev].rsp = saved_rsp;
        } else {
            tasks[prev].rsp = saved_rsp;
        }
    }

#ifdef DEBUG_SCHED
    serial_puts("sched: save prev=");
    serial_hex(prev);
    serial_puts(" rsp=");
    serial_hex(saved_rsp);
    serial_puts("\n");
#endif

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && scheduler_ticks_total >= tasks[i].sleep_until) {
            tasks[i].state = TASK_READY;
            tasks[i].sleep_until = 0;
        }
        /* Wake blocked tasks whose waited-for child is now ZOMBIE */
        if (tasks[i].state == TASK_BLOCKED) {
            int wpid = tasks[i].wait_for_pid;
            for (int j = 0; j < MAX_TASKS; j++) {
                if (tasks[j].state != TASK_ZOMBIE) continue;
                if (tasks[j].parent_pid != i) continue;
                if (wpid != -1 && j != wpid) continue;
                tasks[i].state = TASK_READY;
                tasks[i].wait_for_pid = -1;
                break;
            }
        }
    }

    int next = -1;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (prev + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_READY) {
            next = idx;
            break;
        }
    }

    if (next < 0) {
        if (prev >= 0 && (tasks[prev].state == TASK_READY || tasks[prev].state == TASK_RUNNING)) {
            next = prev;
        } else {
            for (int i = 0; i < MAX_TASKS; i++) {
                if (tasks[i].state == TASK_READY) {
                    next = i;
                    break;
                }
            }
        }
    }

    if (next < 0) return;

    /* Free deferred old kernel stack from exec (if any).
     * By this point we've saved prev's new RSP into saved_rsp, so the
     * old kernel stack is no longer in use. */
    if (prev >= 0 && tasks[prev].pending_free_kstack) {
        kfree(tasks[prev].pending_free_kstack);
        tasks[prev].pending_free_kstack = NULL;
    }

#ifdef DEBUG_SCHED
    serial_puts("sched: prev=");
    serial_hex(prev);
    serial_puts(" next=");
    serial_hex(next);
    serial_puts(" rsp=");
    serial_hex(tasks[next].rsp);
    serial_puts("\n");

    /* Dump iretq frame for debugging */
    {
        uint64_t *fr = (uint64_t *)tasks[next].rsp;
        serial_puts("  iretq: rip=");
        serial_hex(fr[17]);
        serial_puts(" cs=");
        serial_hex(fr[18]);
        serial_puts(" rfl=");
        serial_hex(fr[19]);
        if (fr[18] & 3) {
            serial_puts(" rsp=");
            serial_hex(fr[20]);
            serial_puts(" ss=");
            serial_hex(fr[21]);
        }
        serial_puts("\n");
        if (next == 1) {
            int base = (fr[18] & 3) ? 22 : 20;
            serial_puts("  above: ");
            for (int i = 0; i < 8; i++) {
                serial_hex(fr[base+i]);
                serial_puts(" ");
            }
            serial_puts("\n");
        }
    }
#endif

    current_task = next;
    tasks[next].state = TASK_RUNNING;
    tasks[next].ticks++;
    saved_rsp = tasks[next].rsp;

    if (tasks[next].cr3) {
        vmm_switch_pml4(tasks[next].cr3);
    }

    /* Set TSS rsp0 to this task's kernel stack top so interrupts/syscalls
     * from ring 3 use the correct kernel stack */
    uint64_t kstack_top;
    if (tasks[next].kernel_stack) {
        kstack_top = (uint64_t)tasks[next].kernel_stack + TASK_STACK_SIZE;
    } else if (tasks[next].stack) {
        kstack_top = (uint64_t)tasks[next].stack + TASK_STACK_SIZE;
    } else {
        kstack_top = 0;  /* should never happen - idle always has a stack */
    }
    tss_set_rsp0(kstack_top);
}

void scheduler_tick(void) {
    timer_ticks++;
    scheduler_ticks_total++;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && scheduler_ticks_total >= tasks[i].sleep_until) {
            tasks[i].state = TASK_READY;
            tasks[i].sleep_until = 0;
        }
    }

    if (sched_initialized && task_count() > 1) {
        if (current_task >= 0) {
            int quantum = 4 + (tasks[current_task].priority * 2);
            if (timer_ticks % quantum == 0) {
                need_reschedule = 1;
            }
        } else {
            need_reschedule = 1;
        }
    }
}

void scheduler_yield(void) {
    if (sched_initialized && task_count() > 1) {
        need_reschedule = 1;
    }
}
