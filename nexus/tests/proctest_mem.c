#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <crius/abi.h>
#include "api.h"
#include "nexus/syscall.h"
#include "proctest_internal.h"

/* Test 18: Memory leak - 100x fork -> exit -> wait, verify free pages unchanged */
void test_mem_leak_fork(void) {
    prog_print_color("test_mem_leak_fork\n", 0x00FFFF00, 0);
    struct mem_stats before, after;
    if (getmemstats(&before) < 0) { fail("mem_leak_fork"); return; }

    int N = 100;
    int failures = 0;
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) { failures++; continue; }
        if (pid == 0) { exit(0); }
        int status; waitpid(pid, &status, 0); int code = WEXITSTATUS(status);
        if (code != 0) failures++;
    }

    if (getmemstats(&after) < 0) { fail("mem_leak_fork"); return; }

    if (failures > 0) {
        fail("mem_leak_fork");
        klog("mem_leak_fork: failures during test\n");
        return;
    }
    if (after.free_pages == before.free_pages) {
        ok("mem_leak_fork");
    } else {
        fail("mem_leak_fork");
        klog("mem_leak_fork: pages leaked - before/after free_pages differ\n");
    }
}

/* Test 19: Memory leak - 100x fork -> exec -> exit -> wait */
void test_mem_leak_exec(void) {
    prog_print_color("test_mem_leak_exec\n", 0x00FFFF00, 0);
    struct mem_stats before, after;
    if (getmemstats(&before) < 0) { fail("mem_leak_exec"); return; }

    int N = 100;
    int failures = 0;
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) { failures++; continue; }
        if (pid == 0) {
            char *argv[] = { "echo", "x", NULL };
            execv("/bin/echo", argv);
            exit(1);
        }
        int status; waitpid(pid, &status, 0); int code = WEXITSTATUS(status);
        if (code != 0) failures++;
    }

    if (getmemstats(&after) < 0) { fail("mem_leak_exec"); return; }

    if (failures > 0) {
        fail("mem_leak_exec");
        klog("mem_leak_exec: failures during test\n");
        return;
    }
    if (after.free_pages == before.free_pages) {
        ok("mem_leak_exec");
    } else {
        fail("mem_leak_exec");
        klog("mem_leak_exec: pages leaked - before/after free_pages differ\n");
    }
}

/* Test 20: Memory leak - 100x fork -> failed exec -> exit -> wait */
void test_mem_leak_failed_exec(void) {
    prog_print_color("test_mem_leak_failed_exec\n", 0x00FFFF00, 0);
    struct mem_stats before, after;
    if (getmemstats(&before) < 0) { fail("mem_leak_failed_exec"); return; }

    int N = 100;
    int failures = 0;
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) { failures++; continue; }
        if (pid == 0) {
            char *argv[] = { "noexist", NULL };
            execv("/bin/nonexistent_program", argv);
            exit(99);
        }
        int status; waitpid(pid, &status, 0); int code = WEXITSTATUS(status);
        if (code != 99) failures++;
    }

    if (getmemstats(&after) < 0) { fail("mem_leak_failed_exec"); return; }

    if (failures > 0) {
        fail("mem_leak_failed_exec");
        klog("mem_leak_failed_exec: failures during test\n");
        return;
    }
    if (after.free_pages == before.free_pages) {
        ok("mem_leak_failed_exec");
    } else {
        fail("mem_leak_failed_exec");
        klog("mem_leak_failed_exec: pages leaked - before/after free_pages differ\n");
    }
}

/* Test 21: Memory leak - mixed fork/exec/failed-exec cycles */
void test_mem_leak_mixed(void) {
    prog_print_color("test_mem_leak_mixed\n", 0x00FFFF00, 0);
    struct mem_stats before, after;
    if (getmemstats(&before) < 0) { fail("mem_leak_mixed"); return; }

    int N = 50;
    int failures = 0;
    for (int i = 0; i < N; i++) {
        /* Cycle 0: fork -> exit -> wait */
        {
            pid_t pid = fork();
            if (pid < 0) { failures++; continue; }
            if (pid == 0) { exit(0); }
            waitpid(pid, NULL, 0);
        }
        /* Cycle 1: fork -> exec -> exit -> wait */
        {
            pid_t pid = fork();
            if (pid < 0) { failures++; continue; }
            if (pid == 0) {
                char *argv[] = { "echo", "x", NULL };
                execv("/bin/echo", argv);
                exit(1);
            }
            waitpid(pid, NULL, 0);
        }
        /* Cycle 2: fork -> failed exec -> exit -> wait */
        {
            pid_t pid = fork();
            if (pid < 0) { failures++; continue; }
            if (pid == 0) {
                char *argv[] = { "noexist", NULL };
                execv("/bin/nonexistent", argv);
                exit(99);
            }
            waitpid(pid, NULL, 0);
        }
    }

    if (getmemstats(&after) < 0) { fail("mem_leak_mixed"); return; }

    if (failures > 0) {
        fail("mem_leak_mixed");
        klog("mem_leak_mixed: failures during test\n");
        return;
    }
    if (after.free_pages == before.free_pages) {
        ok("mem_leak_mixed");
    } else {
        fail("mem_leak_mixed");
        klog("mem_leak_mixed: pages leaked - before/after free_pages differ\n");
    }
}
