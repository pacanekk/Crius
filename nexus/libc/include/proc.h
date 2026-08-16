#ifndef NEXUS_PROC_H
#define NEXUS_PROC_H

#include <sys/types.h>
#include <crius/abi.h>

/* get_proc_info(pid, &info) → 0 on success, -1 on error */
int get_proc_info(pid_t pid, struct proc_info *info);

#endif
