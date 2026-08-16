#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <crius/abi.h>
#include "task.h"

extern volatile uint64_t syscall_saved_rsp;

void scheduler_init(void);
void scheduler_tick(void);
void scheduler_yield(void);

int task_create(const char *name, void (*entry)(void));
int task_create_args(const char *name, void (*entry)(void *), void *arg);
int task_create_current(const char *name);
void task_exit_code(int code);
int task_wait(int pid, int *status, int options);
int task_kill(int id);
void task_sleep(uint64_t ms);
void task_set_priority(int id, int priority);
void task_cleanup(struct task *t);
void task_reparent_children(int dying_pid);

struct task *task_current(void);
struct task *task_get(int i);
int task_current_id(void);
int task_count(void);
uint64_t task_get_cr3(int id);
int task_fork(uint64_t saved_rsp);

#endif
