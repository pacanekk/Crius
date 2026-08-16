#ifndef NEXUS_SYS_WAIT_H
#define NEXUS_SYS_WAIT_H

#include <sys/types.h>

/* wait(pid) → pid on success, -1 on error */
pid_t wait(pid_t pid);

#endif
