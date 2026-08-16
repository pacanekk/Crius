#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "api.h"
#include "nexus/syscall.h"

static int tests_passed = 0;
static int tests_failed = 0;

static void test_print(const char *s) {
    prog_print_color(s, 0x00FF00, 0x000000);
}

static void test_fail(const char *s) {
    prog_print_color(s, 0xFF0000, 0x000000);
}

static void test_name(const char *s) {
    prog_print_color("  ", 0x00FFFFFF, 0);
    prog_print_color(s, 0x00FFFF, 0x000000);
    prog_print_color(": ", 0x00FFFF, 0x000000);
}

static void test_ok(void) {
    test_print("OK\n");
    tests_passed++;
}

static void test_no(void) {
    test_fail("FAIL\n");
    tests_failed++;
}

int security_test_main(int argc, char **argv) {
    (void)argc; (void)argv;

    prog_print_color("=== Security Tests ===\n", 0x00FFFF, 0x000000);

    /* Test 1: kernel pointer to open() - should return -1 */
    test_name("T1:kernel_ptr_open");
    {
        long r = _syscall2(SYS_OPEN, 0xFFFF800000000000UL, 0);
        if ((uint64_t)r == (uint64_t)-1) test_ok();
        else test_no();
    }

    /* Test 2: unmapped low address to fd read - should return -1 */
    test_name("T2:bad_ptr_read");
    {
        long r = _syscall3(SYS_READ, 0, 0x123, 100);
        if ((uint64_t)r == (uint64_t)-1) test_ok();
        else test_no();
    }

    /* Test 3: kernel pointer to fd write - should return -1 */
    test_name("T3:kernel_ptr_write");
    {
        long r = _syscall3(SYS_WRITE, 1, 0xFFFF800000000000UL, 10);
        if ((uint64_t)r == (uint64_t)-1) test_ok();
        else test_no();
    }

    /* Test 4: bad fd to read - should return -1 */
    test_name("T4:bad_fd_read");
    {
        char tmp[16];
        long r = _syscall3(SYS_READ, 99, (long)tmp, 10);
        if ((uint64_t)r == (uint64_t)-1) test_ok();
        else test_no();
    }

    /* Test 5: kernel pointer to proc info - should return -1 */
    test_name("T5:kernel_ptr_proc_info");
    {
        long r = _syscall2(SYS_GETPROCINFO, 1, 0xFFFF800000000000UL);
        if ((uint64_t)r == (uint64_t)-1) test_ok();
        else test_no();
    }

    /* Test 6: unknown syscall should return -1 */
    test_name("T6:unknown_syscall");
    {
        long r = _syscall0(200);
        if ((uint64_t)r == (uint64_t)-1) test_ok();
        else test_no();
    }

    /* Test 7: valid getcwd should work */
    test_name("T7:valid_getcwd");
    {
        char buf[128];
        long r = _syscall2(SYS_GETCWD, (long)buf, 128);
        if (r == 0) test_ok();
        else test_no();
    }

    /* Summary */
    prog_print_color("\n=== Results: ", 0x00FFFF, 0x000000);
    char nbuf[16];
    int n = 0;
    int v = tests_passed;
    if (v == 0) { nbuf[n++] = '0'; }
    else { char tmp[16]; int t = 0; while (v) { tmp[t++] = '0' + (v % 10); v /= 10; } while (t > 0) nbuf[n++] = tmp[--t]; }
    nbuf[n] = '\0';
    prog_print_color(nbuf, 0x00FF00, 0);
    prog_print_color(" passed, ", 0x00FFFF, 0);

    v = tests_failed;
    n = 0;
    if (v == 0) { nbuf[n++] = '0'; }
    else { char tmp[16]; int t = 0; while (v) { tmp[t++] = '0' + (v % 10); v /= 10; } while (t > 0) nbuf[n++] = tmp[--t]; }
    nbuf[n] = '\0';
    prog_print_color(nbuf, 0xFF0000, 0);
    prog_print_color(" failed ===\n", 0x00FFFF, 0);

    return 0;
}
