#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fs/file.h"

#define MAX_TASKS 64
#define TASK_NAME_LEN 64
#define TASK_STACK_SIZE 32768
#define TASK_DEFAULT_PRIORITY 2
#define TASK_MAX_PRIORITY 5
#define MAX_FDS 32

enum task_state {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_ZOMBIE,
};

struct task {
    /* thread fields */
    enum task_state state;
    uint64_t rsp;
    uint64_t rip;
    uint64_t ticks;
    void *stack;           /* legacy: used as kernel stack for ring 0 tasks */
    void *kernel_stack;    /* dedicated kernel stack for ring 3 tasks */
    uint64_t user_rsp;     /* user stack pointer (ring 3) */
    int priority;
    uint64_t sleep_until;
    void *arg;

    /* process fields */
    char name[TASK_NAME_LEN];
    uint64_t cr3;
    int parent_pid;
    int first_child;       /* PID of first child (-1 if none) */
    int exit_code;
    int wait_for_pid;      /* pid this task is waiting on (-1 = any child) */
    int next_sibling;      /* PID of next sibling with same parent (-1 = none) */
    void *pending_free_kstack; /* old kernel stack to free after context switch */
    char cwd[128];         /* per-process working directory */
    struct file *fds[MAX_FDS];
};

#endif
