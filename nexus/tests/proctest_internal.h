#ifndef PROCTEST_INTERNAL_H
#define PROCTEST_INTERNAL_H

extern int tests_passed;
extern int tests_failed;

void ok(const char *name);
void fail(const char *name);
void puti(int n);

void test_exit_code(void);
void test_concurrent(void);
void test_fork_exec(void);
void test_crash(void);
void test_orphan(void);
void test_double_exec(void);
void test_kill_reparent(void);
void test_wait_any(void);
void test_pid_reuse(void);
void test_stress_forks(void);
void test_zombie_cleanup(void);
void test_fd_inherit(void);
void test_fd_kill_cleanup(void);
void test_fd_exec_persist(void);
void test_zombie_delayed_wait(void);
void test_exec_failure(void);
void test_sibling_consistency(void);
void test_mem_leak_fork(void);
void test_mem_leak_exec(void);
void test_mem_leak_failed_exec(void);
void test_mem_leak_mixed(void);

#endif
