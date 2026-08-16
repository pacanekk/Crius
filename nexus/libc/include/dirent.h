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

/* Directory entry returned by readdir() */
struct dirent {
    ino_t         d_ino;
    unsigned char d_type;
    char          d_name[256];
};

/* Opaque directory stream */
typedef struct __dirstream DIR;

/* Open a directory - returns DIR* on success, NULL on error */
DIR *opendir(const char *path);

/* Read next directory entry - returns pointer to entry, NULL on EOF/error */
struct dirent *readdir(DIR *dir);

/* Close a directory - returns 0 on success, -1 on error */
int closedir(DIR *dir);

/* Rewind directory stream to the beginning */
void rewinddir(DIR *dir);

#endif
