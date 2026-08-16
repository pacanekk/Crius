#ifndef NEXUS_FCNTL_H
#define NEXUS_FCNTL_H

/* Open flags */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREATE    4
#define O_APPEND    8
#define O_TRUNC     16

/* File types (from stat) */
#define S_IFREG     1   /* regular file */
#define S_IFDIR     2   /* directory */
#define S_IFBLK     3   /* block device / dev */

/* open(path, flags) → fd on success, -1 on error */
int open(const char *path, int flags);

/* close(fd) → 0 on success, -1 on error */
int close(int fd);

#endif
