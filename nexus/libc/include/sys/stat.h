#ifndef NEXUS_SYS_STAT_H
#define NEXUS_SYS_STAT_H

#include <sys/types.h>

struct stat {
    int     type;       /* FILE_TYPE_FILE / DIR / DEV */
    size_t  size;
};

/* stat(path, &st) → 0 on success, -1 on error */
int stat(const char *path, struct stat *st);

#endif
