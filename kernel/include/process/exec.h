#ifndef PROCESS_EXEC_H
#define PROCESS_EXEC_H

#include <stddef.h>

/*
 * do_exec - replace current process image with a new ELF.
 * Returns 0 on success (never returns to caller in the normal case,
 * the syscall frame is modified to jump to the new entry point).
 * Returns -1 on failure.
 */
int do_exec(const char *path, char **argv, char **envp);

#endif
