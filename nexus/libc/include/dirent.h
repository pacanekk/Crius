#ifndef NEXUS_DIRENT_H
#define NEXUS_DIRENT_H

#include <stdint.h>

/* File type constants */
#define FILE_TYPE_FILE      1
#define FILE_TYPE_DIR       2
#define FILE_TYPE_DEV       3

/* Directory entry returned by read() on a directory fd */
struct dirent {
    uint32_t inode;
    uint8_t  type;
    char     name[256];
};

/* Open a directory for reading - returns fd, or -1 on error */
int opendir(const char *path);

/* Read next directory entry - returns 1 on success, 0 on EOF, -1 on error */
int readdir(int fd, struct dirent *entry);

#endif
