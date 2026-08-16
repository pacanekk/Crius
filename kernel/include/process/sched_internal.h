#ifndef SCHED_INTERNAL_H
#define SCHED_INTERNAL_H

#include "process/task.h"

/* Shared between scheduler.c, process/process.c, process/fork.c */

extern struct task tasks[MAX_TASKS];
extern int current_task;
extern volatile uint64_t scheduler_ticks_total;
extern volatile int need_reschedule;
extern volatile uint64_t saved_rsp;
extern volatile uint64_t syscall_saved_rsp;
extern int sched_initialized;

int alloc_task(void);

#endif
