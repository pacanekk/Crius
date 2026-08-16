#ifndef NEXUS_DIRENT_H
#define NEXUS_DIRENT_H

#include <stdint.h>
#include <sys/types.h>
#include <crius/abi.h>

/* d_type aliases for ABI file types */
#define DT_REG   FILE_TYPE_FILE
#define DT_DIR   FILE_TYPE_DIR
#define DT_BLK   FILE_TYPE_DEV
#define DT_CHR   4   /* Crius currently does not distinguish char vs block devices */
#define DT_FIFO  5
#define DT_LNK   6
#define DT_SOCK  7

/* Directory entry returned by read() on a directory fd */
struct dirent {
    ino_t         d_ino;
    unsigned char d_type;
    char          d_name[256];
};

/* Open a directory for reading - returns fd, or -1 on error */
int opendir(const char *path);

/* Read next directory entry - returns 1 on success, 0 on EOF, -1 on error */
int readdir(int fd, struct dirent *entry);

#endif
