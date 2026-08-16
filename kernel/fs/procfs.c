#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <crius/abi.h>
#include "fs/ramfs.h"
#include "process/scheduler.h"
#include "mm/pmm.h"

/* ===== String helpers ===== */

static void put_str(char *buf, int *pos, int bufsize, const char *s) {
    while (*s && *pos < bufsize - 1) buf[(*pos)++] = *s++;
}

static void put_uint(char *buf, int *pos, int bufsize, uint64_t val) {
    if (val == 0) { if (*pos < bufsize - 1) buf[(*pos)++] = '0'; return; }
    char tmp[20]; int tm = 0;
    while (val) { tmp[tm++] = '0' + (val % 10); val /= 10; }
    while (tm > 0 && *pos < bufsize - 1) buf[(*pos)++] = tmp[--tm];
}

/* ===== /proc callbacks ===== */

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf));
}

static int proc_cpuinfo_read(char *buf, size_t bufsize) {
    int pos = 0;

    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);

    put_str(buf, &pos, bufsize, "processor : 0\n");

    char vendor[13];
    vendor[0] = b & 0xFF;
    vendor[1] = (b >> 8) & 0xFF;
    vendor[2] = (b >> 16) & 0xFF;
    vendor[3] = (b >> 24) & 0xFF;
    vendor[4] = d & 0xFF;
    vendor[5] = (d >> 8) & 0xFF;
    vendor[6] = (d >> 16) & 0xFF;
    vendor[7] = (d >> 24) & 0xFF;
    vendor[8] = c & 0xFF;
    vendor[9] = (c >> 8) & 0xFF;
    vendor[10] = (c >> 16) & 0xFF;
    vendor[11] = (c >> 24) & 0xFF;
    vendor[12] = '\0';

    put_str(buf, &pos, bufsize, "vendor_id : ");
    put_str(buf, &pos, bufsize, vendor);
    put_str(buf, &pos, bufsize, "\n");

    cpuid(1, &a, &b, &c, &d);
    put_str(buf, &pos, bufsize, "cpu_family : ");
    put_uint(buf, &pos, bufsize, (a >> 8) & 0xF);
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "model : ");
    put_uint(buf, &pos, bufsize, (a >> 4) & 0xF);
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "stepping : ");
    put_uint(buf, &pos, bufsize, a & 0xF);
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "features : ");
    if (d & (1 << 0))  put_str(buf, &pos, bufsize, "fpu ");
    if (d & (1 << 23)) put_str(buf, &pos, bufsize, "mmx ");
    if (d & (1 << 25)) put_str(buf, &pos, bufsize, "sse ");
    if (d & (1 << 26)) put_str(buf, &pos, bufsize, "sse2 ");
    if (c & (1 << 0))  put_str(buf, &pos, bufsize, "sse3 ");
    if (c & (1 << 9))  put_str(buf, &pos, bufsize, "ssse3 ");
    if (c & (1 << 20)) put_str(buf, &pos, bufsize, "sse4.2 ");
    if (c & (1 << 28)) put_str(buf, &pos, bufsize, "avx ");
    if (d & (1 << 4))  put_str(buf, &pos, bufsize, "tsc ");
    if (d & (1 << 5))  put_str(buf, &pos, bufsize, "msr ");
    if (d & (1 << 8))  put_str(buf, &pos, bufsize, "apic ");
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "cores : 1\n");

    buf[pos] = '\0';
    return pos;
}

static int proc_meminfo_read(char *buf, size_t bufsize) {
    int pos = 0;
    size_t total_pages, free_pages;
    pmm_stats(&total_pages, &free_pages);

    size_t total_kb = total_pages * 4;
    size_t free_kb = free_pages * 4;
    size_t used_kb = total_kb - free_kb;

    put_str(buf, &pos, bufsize, "MemTotal: ");
    put_uint(buf, &pos, bufsize, total_kb);
    put_str(buf, &pos, bufsize, " kB\n");

    put_str(buf, &pos, bufsize, "MemFree: ");
    put_uint(buf, &pos, bufsize, free_kb);
    put_str(buf, &pos, bufsize, " kB\n");

    put_str(buf, &pos, bufsize, "MemUsed: ");
    put_uint(buf, &pos, bufsize, used_kb);
    put_str(buf, &pos, bufsize, " kB\n");

    buf[pos] = '\0';
    return pos;
}

static int proc_uptime_read(char *buf, size_t bufsize) {
    extern volatile uint64_t scheduler_ticks_total;
    int pos = 0;
    put_uint(buf, &pos, bufsize, scheduler_ticks_total);
    put_str(buf, &pos, bufsize, " ticks\n");
    buf[pos] = '\0';
    return pos;
}

static int proc_self_status_read(char *buf, size_t bufsize) {
    struct task *t = task_current();
    if (!t) {
        int pos = 0;
        put_str(buf, &pos, bufsize, "No current task\n");
        buf[pos] = '\0';
        return pos;
    }
    int pos = 0;

    put_str(buf, &pos, bufsize, "Name: ");
    put_str(buf, &pos, bufsize, t->name);
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "State: ");
    char state = t->state == TASK_RUNNING ? 'R' :
                 t->state == TASK_READY   ? 'R' :
                 t->state == TASK_ZOMBIE  ? 'Z' : 'S';
    if (pos < (int)bufsize - 1) buf[pos++] = state;
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "Ticks: ");
    put_uint(buf, &pos, bufsize, t->ticks);
    put_str(buf, &pos, bufsize, "\n");

    put_str(buf, &pos, bufsize, "PID: ");
    put_uint(buf, &pos, bufsize, (uint64_t)task_current_id());
    put_str(buf, &pos, bufsize, "\n");

    buf[pos] = '\0';
    return pos;
}

static int proc_list_read(char *buf, size_t bufsize) {
    int pos = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        struct task *t = task_get(i);
        if (!t || t->state == TASK_UNUSED) continue;
        put_str(buf, &pos, bufsize, "[");
        put_uint(buf, &pos, bufsize, (uint64_t)i);
        put_str(buf, &pos, bufsize, "] ");
        put_str(buf, &pos, bufsize, t->name);
        put_str(buf, &pos, bufsize, "\n");
    }
    buf[pos] = '\0';
    return pos;
}

/* ===== procfs_init - called from vfs_init ===== */

void procfs_init(void) {
    ramfs_mkdir("/proc");

    ramfs_create_dev("/proc/cpuinfo", proc_cpuinfo_read, NULL);
    ramfs_create_dev("/proc/meminfo", proc_meminfo_read, NULL);
    ramfs_create_dev("/proc/uptime", proc_uptime_read, NULL);
    ramfs_create_dev("/proc/list", proc_list_read, NULL);
    ramfs_mkdir("/proc/self");
    ramfs_create_dev("/proc/self/status", proc_self_status_read, NULL);
}
