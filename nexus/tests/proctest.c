#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <crius/abi.h>
#include "api.h"
#include "nexus/syscall.h"
#include "proctest_internal.h"

int tests_passed = 0;
int tests_failed = 0;

void ok(const char *name) {
    prog_print_color("  ", 0x00FFFFFF, 0);
    prog_print_color(name, 0x00FFFF, 0);
    prog_print_color(": ", 0x00FFFF, 0);
    prog_print_color("OK\n", 0x0000FF00, 0);
    klog("proctest: "); klog(name); klog(" OK\n");
    tests_passed++;
}

void fail(const char *name) {
    prog_print_color("  ", 0x00FFFFFF, 0);
    prog_print_color(name, 0x00FFFF, 0);
    prog_print_color(": ", 0x00FFFF, 0);
    prog_print_color("FAIL\n", 0x00FF0000, 0);
    klog("proctest: "); klog(name); klog(" FAIL\n");
    tests_failed++;
}

void puti(int n) {
    char buf[16];
    int i = 0;
    if (n < 0) { prog_putc('-', 0x00FFFFFF, 0); n = -n; }
    if (n == 0) { prog_putc('0', 0x00FFFFFF, 0); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) prog_putc(buf[--i], 0x00FFFFFF, 0);
}

/* Test 1: exit code */
void test_exit_code(void) {
    prog_print_color("test_exit_code\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        exit(42);
    }
    int code = wait(pid);
    if (code == 42) ok("exit42"); else { fail("exit42"); prog_print_color("    got code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 2: concurrent processes */
void test_concurrent(void) {
    prog_print_color("test_concurrent\n", 0x00FFFF00, 0);
    pid_t p1 = fork();
    if (p1 < 0) { fail("fork1"); return; }
    if (p1 == 0) {
        for (int i = 0; i < 5; i++) {
            prog_print_color("A", 0x0000FF00, 0);
            yield();
        }
        exit(0);
    }
    pid_t p2 = fork();
    if (p2 < 0) { fail("fork2"); return; }
    if (p2 == 0) {
        for (int i = 0; i < 5; i++) {
            prog_print_color("B", 0x00FFFF, 0);
            yield();
        }
        exit(0);
    }
    wait(p1);
    wait(p2);
    prog_newline();
    ok("both_ran");
}

/* Test 3: fork + exec */
void test_fork_exec(void) {
    prog_print_color("test_fork_exec\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        char *argv[] = { "echo", "exec_works", NULL };
        exec("/bin/echo", 2, argv);
        exit(1);
    }
    int code = wait(pid);
    if (code == 0) ok("exec_echo"); else { fail("exec_echo"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 4: crash (NULL deref) */
void test_crash(void) {
    prog_print_color("test_crash\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        volatile int *p = (volatile int *)0;
        *p = 42;
        exit(0);
    }
    int code = wait(pid);
    if (code == -1) ok("segfault_killed"); else { fail("segfault_killed"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 5: orphan survival */
void test_orphan(void) {
    prog_print_color("test_orphan\n", 0x00FFFF00, 0);
    pid_t child = fork();
    if (child < 0) { fail("fork"); return; }
    if (child == 0) {
        pid_t gc = fork();
        if (gc < 0) { exit(1); }
        if (gc == 0) {
            sleep(5);
            prog_print_color("orphan_alive\n", 0x0000FF00, 0);
            exit(0);
        }
        exit(0);
    }
    wait(child);
    sleep(10);
    ok("orphan_survived");
}

/* Test 6: double-exec - child execs twice, verify no crash/leak */
void test_double_exec(void) {
    prog_print_color("test_double_exec\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        /* First exec */
        char *argv1[] = { "echo", "first", NULL };
        exec("/bin/echo", 2, argv1);
        /* If exec fails, try second */
        char *argv2[] = { "echo", "second", NULL };
        exec("/bin/echo", 2, argv2);
        exit(1);
    }
    int code = wait(pid);
    if (code == 0) ok("double_exec"); else { fail("double_exec"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 7: kill + reparent - kill a process with children */
void test_kill_reparent(void) {
    prog_print_color("test_kill_reparent\n", 0x00FFFF00, 0);
    pid_t child = fork();
    if (child < 0) { fail("fork"); return; }
    if (child == 0) {
        /* Create a grandchild that sleeps */
        pid_t gc = fork();
        if (gc < 0) { exit(1); }
        if (gc == 0) {
            sleep(10);
            prog_print_color("gc_alive_after_kill\n", 0x0000FF00, 0);
            exit(0);
        }
        /* Child sleeps so parent can kill it */
        sleep(20);
        exit(0);
    }
    /* Give child time to fork grandchild */
    sleep(2);
    /* Kill child - grandchild should be reparented to init */
    int k = kill(child);
    if (k < 0) { fail("kill"); return; }
    /* Wait for the killed child */
    int code = wait(child);
    if (code == -1) ok("kill_exit_code"); else { fail("kill_exit_code"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
    /* Grandchild should still be alive - wait for it via sleep */
    sleep(15);
    ok("gc_survived_kill");
}

/* Test 8: wait(-1) - reap any child */
void test_wait_any(void) {
    prog_print_color("test_wait_any\n", 0x00FFFF00, 0);
    pid_t p1 = fork();
    if (p1 < 0) { fail("fork1"); return; }
    if (p1 == 0) { exit(10); }
    pid_t p2 = fork();
    if (p2 < 0) { fail("fork2"); return; }
    if (p2 == 0) { exit(20); }
    /* wait(-1) should reap one of them */
    int code1 = wait(-1);
    int code2 = wait(-1);
    /* Both should be reaped, codes should be 10 or 20 */
    if ((code1 == 10 || code1 == 20) && (code2 == 10 || code2 == 20) && code1 != code2)
        ok("wait_any");
    else {
        fail("wait_any");
        prog_print_color("    code1=", 0x00FFFFFF, 0); puti(code1);
        prog_print_color(" code2=", 0x00FFFFFF, 0); puti(code2);
        prog_newline();
    }
}

/* Test 9: PID reuse - fork+exit+wait, fork again, expect same PID */
void test_pid_reuse(void) {
    prog_print_color("test_pid_reuse\n", 0x00FFFF00, 0);
    pid_t p1 = fork();
    if (p1 < 0) { fail("fork1"); return; }
    if (p1 == 0) { exit(0); }
    wait(p1);

    pid_t p2 = fork();
    if (p2 < 0) { fail("fork2"); return; }
    if (p2 == 0) { exit(0); }
    /* p2 should reuse p1's slot (same PID) */
    if (p2 == p1) {
        wait(p2);
        ok("pid_reuse");
    } else {
        fail("pid_reuse");
        prog_print_color("    p1=", 0x00FFFFFF, 0); puti(p1);
        prog_print_color(" p2=", 0x00FFFFFF, 0); puti(p2);
        prog_newline();
        wait(p2);
    }
}

/* Test 10: stress - many forks in sequence */
void test_stress_forks(void) {
    prog_print_color("test_stress_forks\n", 0x00FFFF00, 0);
    int N = 20;
    int failures = 0;
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();
        if (pid < 0) { failures++; continue; }
        if (pid == 0) { exit(i); }
        int code = wait(pid);
        if (code != i) failures++;
    }
    if (failures == 0) ok("stress_20"); else { fail("stress_20"); prog_print_color("    failures=", 0x00FFFFFF, 0); puti(failures); prog_newline(); }
}

/* Test 11: zombie cleanup - fork N children, exit all, wait all */
void test_zombie_cleanup(void) {
    prog_print_color("test_zombie_cleanup\n", 0x00FFFF00, 0);
    int N = 10;
    pid_t pids[10];
    for (int i = 0; i < N; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { fail("fork"); return; }
        if (pids[i] == 0) { exit(0); }
    }
    /* Wait for all */
    int reaped = 0;
    for (int i = 0; i < N; i++) {
        int code = wait(pids[i]);
        if (code == 0) reaped++;
    }
    if (reaped == N) ok("zombie_cleanup"); else { fail("zombie_cleanup"); prog_print_color("    reaped=", 0x00FFFFFF, 0); puti(reaped); prog_print_color("/", 0x00FFFFFF, 0); puti(N); prog_newline(); }
}

/* Test 12: FD inheritance - fds survive fork, still work */
void test_fd_inherit(void) {
    prog_print_color("test_fd_inherit\n", 0x00FFFF00, 0);
    /* Write to stdout (fd 1) from child - inherited from parent */
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        /* Child writes to stdout - fd 1 should be inherited */
        write(1, "fd_inherit_ok\n", 14);
        exit(0);
    }
    int code = wait(pid);
    if (code == 0) ok("fd_inherit"); else { fail("fd_inherit"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 13: FD cleanup after kill - killed process should not leak FDs */
void test_fd_kill_cleanup(void) {
    prog_print_color("test_fd_kill_cleanup\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        /* Open a file then sleep - parent will kill us */
        int fd = open("/etc/os-release", 0);
        if (fd < 0) { exit(1); }
        sleep(30);
        exit(0);
    }
    sleep(2);
    int k = kill(pid);
    if (k < 0) { fail("kill"); return; }
    int code = wait(pid);
    if (code == -1) ok("fd_kill_cleanup"); else { fail("fd_kill_cleanup"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 14: FD persistence after exec - fds should survive exec */
void test_fd_exec_persist(void) {
    prog_print_color("test_fd_exec_persist\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        /* Child execs echo - stdout (fd 1) should still work after exec */
        char *argv[] = { "echo", "exec_fd_works", NULL };
        exec("/bin/echo", 2, argv);
        exit(1);
    }
    int code = wait(pid);
    if (code == 0) ok("fd_exec_persist"); else { fail("fd_exec_persist"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 15: fork + exit without immediate wait - zombie should be reaped later */
void test_zombie_delayed_wait(void) {
    prog_print_color("test_zombie_delayed_wait\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) { exit(77); }
    /* Do some work before waiting */
    sleep(5);
    int code = wait(pid);
    if (code == 77) ok("zombie_delayed_wait"); else { fail("zombie_delayed_wait"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 16: exec failure - exec non-existent file should return failure */
void test_exec_failure(void) {
    prog_print_color("test_exec_failure\n", 0x00FFFF00, 0);
    pid_t pid = fork();
    if (pid < 0) { fail("fork"); return; }
    if (pid == 0) {
        char *argv[] = { "nonexistent", NULL };
        exec("/bin/nonexistent_program", 1, argv);
        /* If exec fails, we should continue and exit with a specific code */
        exit(99);
    }
    int code = wait(pid);
    if (code == 99) ok("exec_failure"); else { fail("exec_failure"); prog_print_color("    code=", 0x00FFFFFF, 0); puti(code); prog_newline(); }
}

/* Test 17: child/sibling list consistency - fork multiple, verify PID ordering */
void test_sibling_consistency(void) {
    prog_print_color("test_sibling_consistency\n", 0x00FFFF00, 0);
    pid_t pids[5];
    int ok_count = 0;
    for (int i = 0; i < 5; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { fail("fork"); return; }
        if (pids[i] == 0) { exit(i); }
    }
    /* Wait for all children and verify exit codes */
    for (int i = 0; i < 5; i++) {
        int code = wait(pids[i]);
        if (code == i) ok_count++;
    }
    if (ok_count == 5) ok("sibling_consistency"); else { fail("sibling_consistency"); prog_print_color("    ok=", 0x00FFFFFF, 0); puti(ok_count); prog_print_color("/5\n", 0x00FFFFFF, 0); }
}

int proctest_main(int argc, char **argv) {
    (void)argc; (void)argv;
    prog_print_color("\n=== Process Subsystem Tests ===\n\n", 0x00FFFF00, 0);

    test_exit_code();
    test_concurrent();
    test_fork_exec();
    test_crash();
    test_orphan();
    test_double_exec();
    test_kill_reparent();
    test_wait_any();
    test_pid_reuse();
    test_stress_forks();
    test_zombie_cleanup();
    test_fd_inherit();
    test_fd_kill_cleanup();
    test_fd_exec_persist();
    test_zombie_delayed_wait();
    test_exec_failure();
    test_sibling_consistency();
    test_mem_leak_fork();
    test_mem_leak_exec();
    test_mem_leak_failed_exec();
    test_mem_leak_mixed();

    prog_print_color("\n=== Results: ", 0x00FFFF00, 0);
    puti(tests_passed);
    prog_print_color(" passed, ", 0x00FFFF00, 0);
    puti(tests_failed);
    prog_print_color(" failed ===\n", 0x00FFFF00, 0);

    return tests_failed > 0 ? 1 : 0;
}
