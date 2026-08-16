#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "drivers/serial.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "process/scheduler.h"
#include "process/sched_internal.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "process/userspace_loader.h"
#include "arch/cpu.h"
#include "boot/boot.h"

/* ===== Kernel-level process subsystem tests =====
 * Run before init, with interrupts disabled.
 * Directly inspect tasks[] to verify invariants.
 */
static int ktest_pass = 0;
static int ktest_fail = 0;

static void ktest_ok(const char *name) {
    serial_puts("  [KTEST] ");
    serial_puts(name);
    serial_puts(": OK\n");
    ktest_pass++;
}

static void ktest_fail_msg(const char *name, const char *msg) {
    serial_puts("  [KTEST] ");
    serial_puts(name);
    serial_puts(": FAIL - ");
    serial_puts(msg);
    serial_puts("\n");
    ktest_fail++;
}

static void kernel_process_tests(void) {
    serial_puts("\n=== Kernel Process Tests ===\n");

    /* Test 1: alloc_task returns slot 1 (slot 0 is idle/running) */
    int s1 = alloc_task();
    if (s1 == 1) ktest_ok("alloc_slot1"); else ktest_fail_msg("alloc_slot1", "expected 1");
    tasks[1].state = TASK_RUNNING;
    int s2 = alloc_task();
    if (s2 == 2) ktest_ok("alloc_slot2"); else ktest_fail_msg("alloc_slot2", "expected 2");
    tasks[2].state = TASK_UNUSED;

    /* Test 2: task_create sets up fields correctly.
     * Set slot 1 to ZOMBIE so alloc_task skips it AND
     * task_reparent_children reparents to PID 0 (not PID 1). */
    tasks[1].state = TASK_ZOMBIE;
    tasks[1].first_child = -1;
    tasks[1].next_sibling = -1;
    tasks[2].state = TASK_UNUSED;

    int pid = task_create("testproc", (void (*)(void))0x1234);
    /* pid should be slot 2 */
    if (pid >= 0) {
        /* task_create doesn't update parent's child list (only fork does).
         * Insert manually for test purposes. */
        tasks[pid].next_sibling = tasks[0].first_child;
        tasks[0].first_child = pid;

        if (tasks[pid].state == TASK_READY) ktest_ok("create_state");
        else ktest_fail_msg("create_state", "not READY");

        if (tasks[pid].first_child == -1) ktest_ok("create_first_child");
        else ktest_fail_msg("create_first_child", "not -1");

        if (tasks[pid].next_sibling == -1) ktest_ok("create_next_sibling");
        else ktest_fail_msg("create_next_sibling", "not -1");

        if (tasks[pid].parent_pid == 0) ktest_ok("create_parent");
        else ktest_fail_msg("create_parent", "not 0 (idle)");

        if (tasks[pid].cr3 != 0) ktest_ok("create_cr3");
        else ktest_fail_msg("create_cr3", "cr3 is 0");

        if (tasks[pid].kernel_stack != NULL) ktest_ok("create_kstack");
        else ktest_fail_msg("create_kstack", "kernel_stack is NULL");
    } else {
        ktest_fail_msg("task_create", "returned -1");
    }

    /* Test 3: child/sibling list - idle should now have pid as first_child */
    if (tasks[0].first_child == pid) ktest_ok("idle_has_child");
    else ktest_fail_msg("idle_has_child", "idle.first_child != pid");

    /* Create a grandchild of idle via pid */
    current_task = pid;
    int gpid = task_create("grandchild", (void (*)(void))0x5678);
    current_task = 0;

    if (gpid >= 0) {
        /* Manually insert gpid into pid's child list */
        tasks[gpid].next_sibling = tasks[pid].first_child;
        tasks[pid].first_child = gpid;
    }

    if (gpid >= 0 && tasks[pid].first_child == gpid) ktest_ok("pid_has_grandchild");
    else ktest_fail_msg("pid_has_grandchild", "pid.first_child != gpid");

    /* Test 4: task_kill reparents children.
     * (We can't test task_exit_code directly because it never returns -
     *  it enters a sti;hlt loop.  task_kill tests the same reparenting path.) */
    task_kill(pid);
    if (tasks[pid].state == TASK_ZOMBIE) ktest_ok("kill_zombie");
    else ktest_fail_msg("kill_zombie", "not ZOMBIE");

    if (tasks[pid].exit_code == -1) ktest_ok("kill_exit_code");
    else ktest_fail_msg("kill_exit_code", "not -1");

    /* gpid should now be reparented to idle (PID 0) */
    if (tasks[gpid].parent_pid == 0) ktest_ok("kill_reparent");
    else ktest_fail_msg("kill_reparent", "gpid not reparented to 0");

    /* idle should now have gpid in its child list */
    if (tasks[0].first_child == gpid) ktest_ok("idle_adopted_grandchild");
    else ktest_fail_msg("idle_adopted_grandchild", "idle.first_child != gpid");

    /* pid's first_child should be cleared */
    if (tasks[pid].first_child == -1) ktest_ok("kill_clears_first_child");
    else ktest_fail_msg("kill_clears_first_child", "not -1");

    /* Test 6: task_wait reaps zombie and cleans up */
    /* idle waits for pid (zombie) */
    current_task = 0;
    int code = task_wait(pid);
    if (code == -1) ktest_ok("wait_kill_code");
    else ktest_fail_msg("wait_kill_code", "expected -1");

    if (tasks[pid].state == TASK_UNUSED) ktest_ok("wait_reaped");
    else ktest_fail_msg("wait_reaped", "not UNUSED");

    if (tasks[pid].cr3 == 0) ktest_ok("wait_cr3_cleared");
    else ktest_fail_msg("wait_cr3_cleared", "cr3 not 0");

    if (tasks[pid].kernel_stack == NULL) ktest_ok("wait_kstack_freed");
    else ktest_fail_msg("wait_kstack_freed", "kernel_stack not NULL");

    /* pid should be removed from idle's child list */
    if (tasks[0].first_child != pid) ktest_ok("wait_removes_from_list");
    else ktest_fail_msg("wait_removes_from_list", "pid still in child list");

    /* Test 7: PID reuse - alloc should reuse pid's slot */
    int reused = alloc_task();
    if (reused == pid) ktest_ok("pid_reuse");
    else ktest_fail_msg("pid_reuse", "different slot");

    /* Clean up: mark reused as UNUSED so it doesn't interfere with init */
    tasks[reused].state = TASK_UNUSED;

    /* Test 8: wait(-1) reaps any zombie child */
    /* Create two children of idle, make them zombies, wait(-1) twice */
    int c1 = task_create("child1", (void (*)(void))0xAABB);
    int c2 = task_create("child2", (void (*)(void))0xBBCC);
    if (c1 >= 0 && c2 >= 0) {
        /* Manually insert into idle's child list */
        tasks[c1].next_sibling = tasks[0].first_child;
        tasks[0].first_child = c1;
        tasks[c2].next_sibling = tasks[0].first_child;
        tasks[0].first_child = c2;

        /* Make them zombies */
        tasks[c1].state = TASK_ZOMBIE;
        tasks[c1].exit_code = 11;
        tasks[c2].state = TASK_ZOMBIE;
        tasks[c2].exit_code = 22;

        current_task = 0;
        int r1 = task_wait(-1);
        int r2 = task_wait(-1);
        if ((r1 == 11 || r1 == 22) && (r2 == 11 || r2 == 22) && r1 != r2)
            ktest_ok("wait_any");
        else
            ktest_fail_msg("wait_any", "wrong codes");
    } else {
        ktest_fail_msg("wait_any", "fork failed");
    }

    /* Clean up gpid if still around */
    if (tasks[gpid].state != TASK_UNUSED) {
        tasks[gpid].state = TASK_ZOMBIE;
        current_task = 0;
        task_wait(gpid);
    }

    /* Test 9: FD cleanup after kill - verify fds are NULL after kill.
     * (Can't use real file objects before VFS init; userspace proctest
     *  covers FD cleanup with real open files.) */
    int fd_pid = task_create("fd_test", (void (*)(void))0xCCCC);
    if (fd_pid >= 0) {
        tasks[fd_pid].next_sibling = tasks[0].first_child;
        tasks[0].first_child = fd_pid;
        /* fds should already be NULL from task_create */
        task_kill(fd_pid);
        if (tasks[fd_pid].fds[0] == NULL &&
            tasks[fd_pid].fds[1] == NULL &&
            tasks[fd_pid].fds[2] == NULL)
            ktest_ok("kill_clears_fds");
        else
            ktest_fail_msg("kill_clears_fds", "FDs not NULL after kill");
        /* Reap it */
        current_task = 0;
        task_wait(fd_pid);
    } else {
        ktest_fail_msg("kill_clears_fds", "create failed");
    }

    /* Test 10: pending_free_kstack cleanup after kill */
    int pk_pid = task_create("pk_test", (void (*)(void))0xDDDD);
    if (pk_pid >= 0) {
        tasks[pk_pid].next_sibling = tasks[0].first_child;
        tasks[0].first_child = pk_pid;
        /* Simulate pending_free_kstack from a prior exec */
        void *fake_kstack = kmalloc(1024);
        tasks[pk_pid].pending_free_kstack = fake_kstack;
        task_kill(pk_pid);
        if (tasks[pk_pid].pending_free_kstack == NULL)
            ktest_ok("kill_clears_pending_kstack");
        else
            ktest_fail_msg("kill_clears_pending_kstack", "not NULL after kill");
        current_task = 0;
        task_wait(pk_pid);
    } else {
        ktest_fail_msg("kill_clears_pending_kstack", "create failed");
    }

    /* Test 11: Reparenting self-reference - kill PID 1 should not
     * reparent children back to PID 1 */
    tasks[1].state = TASK_READY;
    tasks[1].first_child = -1;
    tasks[1].next_sibling = -1;
    tasks[1].parent_pid = 0;
    int init_child = task_create("init_child", (void (*)(void))0xEEEE);
    if (init_child >= 0) {
        tasks[init_child].next_sibling = tasks[1].first_child;
        tasks[1].first_child = init_child;
        tasks[init_child].parent_pid = 1;
        /* Kill PID 1 - children should go to PID 0, not back to 1 */
        task_kill(1);
        if (tasks[init_child].parent_pid == 0)
            ktest_ok("reparent_no_self_ref");
        else
            ktest_fail_msg("reparent_no_self_ref", "reparented to dying PID");
        /* Clean up */
        tasks[init_child].state = TASK_ZOMBIE;
        current_task = 0;
        task_wait(init_child);
        tasks[1].state = TASK_UNUSED;
        tasks[1].first_child = -1;
    } else {
        ktest_fail_msg("reparent_no_self_ref", "create failed");
    }

    /* Reset idle's child list and slot 1 */
    tasks[0].first_child = -1;
    tasks[0].next_sibling = -1;
    tasks[0].parent_pid = -1;
    tasks[1].state = TASK_UNUSED;
    tasks[1].first_child = -1;
    tasks[1].next_sibling = -1;

    /* ===== VMM Tests ===== */
    serial_puts("\n=== VMM Tests ===\n");

    /* Test 12: Page-table leak - create PML4, map pages, free, verify PMM */
    {
        size_t free_before, free_after, total_dummy;
        pmm_stats(&total_dummy, &free_before);

        uint64_t pml4 = vmm_create_pml4();
        if (pml4 == 0) { ktest_fail_msg("pt_leak", "create_pml4 failed"); goto vmm_tests_done; }

        /* Map 10 user pages at different virtual addresses to exercise
         * PDPT, PD, and PT allocation */
        int map_ok = 1;
        for (int i = 0; i < 10; i++) {
            uint64_t phys = pmm_alloc_page();
            if (phys == 0) { map_ok = 0; break; }
            if (vmm_map_page(pml4, 0x40000000UL + i * PAGE_SIZE, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0) {
                pmm_free_page(phys);
                map_ok = 0;
                break;
            }
        }
        if (!map_ok) {
            vmm_free_user_pages(pml4);
            pmm_free_page(pml4);
            ktest_fail_msg("pt_leak", "map_page failed");
            goto vmm_tests_done;
        }

        vmm_free_user_pages(pml4);
        pmm_free_page(pml4);

        pmm_stats(&total_dummy, &free_after);
        if (free_after == free_before)
            ktest_ok("pt_leak");
        else {
            serial_puts("  [KTEST] pt_leak: FAIL - before=");
            serial_hex(free_before);
            serial_puts(" after=");
            serial_hex(free_after);
            serial_puts("\n");
            ktest_fail++;
        }
    }

    /* Test 13: Kernel mapping preservation - create+destroy process,
     * verify PML4 entries 256-511 unchanged */
    {
        uint64_t *kern_pml4 = (uint64_t *)(vmm_get_hhdm() + vmm_current_pml4());
        uint64_t saved_kern[256];
        for (int i = 0; i < 256; i++)
            saved_kern[i] = kern_pml4[256 + i];

        /* Create a task (which calls vmm_map_kernel), then destroy it */
        int vmm_pid = task_create("vmm_kern_test", (void (*)(void))0x1234);
        if (vmm_pid >= 0) {
            /* task_create calls vmm_map_kernel which copies entries 256-511 */
            /* Now cleanup the task */
            tasks[vmm_pid].state = TASK_ZOMBIE;
            tasks[vmm_pid].exit_code = 0;
            current_task = 0;
            task_cleanup(&tasks[vmm_pid]);
            tasks[vmm_pid].state = TASK_UNUSED;
            tasks[vmm_pid].cr3 = 0;
            tasks[vmm_pid].kernel_stack = NULL;
            tasks[vmm_pid].stack = NULL;
            tasks[vmm_pid].first_child = -1;
            tasks[vmm_pid].next_sibling = -1;
            tasks[vmm_pid].parent_pid = -1;
        }

        /* Verify kernel mappings are intact */
        int kern_ok = 1;
        for (int i = 0; i < 256; i++) {
            if (kern_pml4[256 + i] != saved_kern[i]) {
                kern_ok = 0;
                break;
            }
        }
        if (kern_ok) ktest_ok("kern_mapping_preserved");
        else ktest_fail_msg("kern_mapping_preserved", "kernel PML4 entries changed");
    }

    /* Test 14: vmm_copy_userspace leak - create src, map pages, copy, free both */
    {
        size_t free_before, free_after, total_dummy;
        pmm_stats(&total_dummy, &free_before);

        uint64_t src_pml4 = vmm_create_pml4();
        uint64_t dst_pml4 = vmm_create_pml4();
        if (!src_pml4 || !dst_pml4) {
            if (src_pml4) { vmm_free_user_pages(src_pml4); pmm_free_page(src_pml4); }
            if (dst_pml4) { vmm_free_user_pages(dst_pml4); pmm_free_page(dst_pml4); }
            ktest_fail_msg("copy_leak", "create_pml4 failed");
            goto vmm_tests_done;
        }

        /* Map 5 pages in src */
        int map_ok = 1;
        for (int i = 0; i < 5; i++) {
            uint64_t phys = pmm_alloc_page();
            if (phys == 0) { map_ok = 0; break; }
            if (vmm_map_page(src_pml4, 0x50000000UL + i * PAGE_SIZE, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0) {
                pmm_free_page(phys);
                map_ok = 0;
                break;
            }
        }
        if (!map_ok) {
            vmm_free_user_pages(src_pml4); pmm_free_page(src_pml4);
            vmm_free_user_pages(dst_pml4); pmm_free_page(dst_pml4);
            ktest_fail_msg("copy_leak", "map failed");
            goto vmm_tests_done;
        }

        /* Copy userspace from src to dst */
        if (vmm_copy_userspace(dst_pml4, src_pml4) < 0) {
            ktest_fail_msg("copy_leak", "copy_userspace failed");
        } else {
            /* Free both */
            vmm_free_user_pages(src_pml4); pmm_free_page(src_pml4);
            vmm_free_user_pages(dst_pml4); pmm_free_page(dst_pml4);

            pmm_stats(&total_dummy, &free_after);
            if (free_after == free_before)
                ktest_ok("copy_leak");
            else {
                serial_puts("  [KTEST] copy_leak: FAIL - before=");
                serial_hex(free_before);
                serial_puts(" after=");
                serial_hex(free_after);
                serial_puts("\n");
                ktest_fail++;
            }
        }
    }

    /* Test 15: Partial address space cleanup - only PML4, no user mappings */
    {
        size_t free_before, free_after, total_dummy;
        pmm_stats(&total_dummy, &free_before);

        uint64_t pml4 = vmm_create_pml4();
        if (pml4) {
            vmm_free_user_pages(pml4);
            pmm_free_page(pml4);
        }

        pmm_stats(&total_dummy, &free_after);
        if (free_after == free_before)
            ktest_ok("partial_cleanup_empty");
        else ktest_fail_msg("partial_cleanup_empty", "leaked pages");
    }

    /* Test 16: Partial address space - PML4 + one PDPT, no PD/PT */
    {
        size_t free_before, free_after, total_dummy;
        pmm_stats(&total_dummy, &free_before);

        uint64_t pml4 = vmm_create_pml4();
        if (pml4) {
            /* Map one page to create PDPT+PD+PT, then free */
            uint64_t phys = pmm_alloc_page();
            if (phys && vmm_map_page(pml4, 0x60000000UL, phys,
                                 PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) == 0) {
                vmm_free_user_pages(pml4);
                pmm_free_page(pml4);
            } else {
                if (phys) pmm_free_page(phys);
                vmm_free_user_pages(pml4);
                pmm_free_page(pml4);
            }
        }

        pmm_stats(&total_dummy, &free_after);
        if (free_after == free_before)
            ktest_ok("partial_cleanup_one_mapping");
        else ktest_fail_msg("partial_cleanup_one_mapping", "leaked pages");
    }

vmm_tests_done:

    serial_puts("\n=== Kernel Tests: ");
    serial_puts(ktest_fail == 0 ? "ALL PASSED" : "FAILURES");
    serial_puts(" ===\n\n");
}

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED(boot_get_base_revision()) == false) {
        hcf();
    }

    boot_early_init();

    /* Run kernel-level process tests before creating idle task.
     * Set current_task=0 to simulate idle as the running task,
     * so task_create uses idle as parent and doesn't auto-set current. */
    current_task = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].first_child = -1;
    tasks[0].next_sibling = -1;
    tasks[0].parent_pid = -1;
    kernel_process_tests();
    /* Reset for real init */
    tasks[0].state = TASK_UNUSED;
    current_task = -1;

    task_create_current("idle");

    serial_puts("kernel: launching first userspace process\n");

    /* Load first userspace ELF module (PID 1) */
    uint64_t init_entry = userspace_load(boot_get_module_response(),
                                      boot_get_hhdm_response()->offset);
    if (init_entry == 0) {
        serial_puts("kernel: FATAL - failed to load userspace module\n");
        hcf();
    }

    int init_pid = task_create_args("init", (void (*)(void *))init_entry, NULL);
    userspace_map_to_task(task_get_cr3(init_pid));

    /* Set up stdin/stdout/stderr for init (PID 1) */
    struct task *init_task = task_get(init_pid);
    if (init_task) {
        init_task->fds[0] = vfs_open("/dev/stdin", O_RDONLY);
        init_task->fds[1] = vfs_open("/dev/stdout", O_WRONLY);
        init_task->fds[2] = vfs_open("/dev/stderr", O_WRONLY);
    }

    serial_puts("kernel: main loop\n");

    __asm__ volatile ("sti");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
