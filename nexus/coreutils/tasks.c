#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <proc.h>
#include "api.h"

int ps_main(int argc, char **argv) {
    (void)argc; (void)argv;
    prog_print_color("ID  STATE     TICKS     PRIO  NAME\n", 0x00FFFFFF, 0);
    int cur = getpid();
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc_info info;
        if (get_proc_info(i, &info) < 0) continue;
        if (info.state == PROC_UNUSED) continue;
        char id = '0' + i;
        prog_putc(id, 0x00FFFFFF, 0);
        prog_print_color("   ", 0x00FFFFFF, 0);
        const char *st = info.state == PROC_RUNNING  ? "RUNNING" :
                         info.state == PROC_READY    ? "READY" :
                         info.state == PROC_ZOMBIE   ? "ZOMBIE" :
                         info.state == PROC_SLEEPING ? "SLEEPING" : "BLOCKED";
        prog_print_color(st, 0x00FFFFFF, 0);
        int sl = 0; while (st[sl]) sl++;
        for (int k = sl; k < 10; k++) prog_putc(' ', 0x00FFFFFF, 0);
        char tbuf[16]; int tn = 0;
        uint64_t ticks = info.ticks;
        if (ticks == 0) { tbuf[tn++] = '0'; }
        else { char tmp[16]; int tm = 0; while (ticks) { tmp[tm++] = '0' + (ticks % 10); ticks /= 10; } while (tm > 0) tbuf[tn++] = tmp[--tm]; }
        tbuf[tn] = '\0';
        prog_print_color(tbuf, 0x00FFFFFF, 0);
        for (int k = tn; k < 10; k++) prog_putc(' ', 0x00FFFFFF, 0);
        char pbuf[4]; pbuf[0] = '0' + info.priority; pbuf[1] = '\0';
        prog_print_color(pbuf, 0x00FFFFFF, 0);
        prog_print_color("     ", 0x00FFFFFF, 0);
        prog_print_color(info.name, 0x00FFFFFF, 0);
        if (i == cur) prog_print_color(" *", 0x0000FF00, 0);
        prog_newline();
    }
    return 0;
}

static int parse_int(const char *s) {
    int v = 0;
    for (int i = 0; s[i]; i++) v = v * 10 + (s[i] - '0');
    return v;
}

int kill_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: kill <id>\n", 0x00FF0000, 0); return 1; }
    int id = parse_int(argv[1]);
    if (kill(id) < 0) {
        prog_print_color("Cannot kill task\n", 0x00FF0000, 0);
        return 1;
    }
    prog_print_color("Killed task ", 0x0000FF00, 0);
    char idbuf[4]; idbuf[0] = '0' + id; idbuf[1] = '\0';
    prog_print_color(idbuf, 0x00FFFF00, 0);
    prog_newline();
    return 0;
}

int sleep_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: sleep <ms>\n", 0x00FF0000, 0); return 1; }
    unsigned ms = (unsigned)parse_int(argv[1]);
    prog_print_color("Sleeping...\n", 0x00FFFF00, 0);
    sleep(ms);
    prog_print_color("Woke up\n", 0x0000FF00, 0);
    return 0;
}

int priority_main(int argc, char **argv) {
    if (argc < 3) { prog_print_color("Usage: priority <id> <0-5>\n", 0x00FF0000, 0); return 1; }
    setpriority(parse_int(argv[1]), parse_int(argv[2]));
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}
