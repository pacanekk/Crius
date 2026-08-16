#ifndef NEXUS_FCNTL_H
#define NEXUS_FCNTL_H

#include <crius/abi.h>
#include <sys/types.h>

/* open(path, flags) → fd on success, -1 on error */
int open(const char *path, int flags);

/* close(fd) → 0 on success, -1 on error */
int close(int fd);

#endif
