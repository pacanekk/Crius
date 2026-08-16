#ifndef EXT2_H
#define EXT2_H

#include <stdint.h>
#include <stddef.h>
#include "fs/file.h"
#include "fs/vfs.h"

#define EXT2_SUPER_OFFSET    1024
#define EXT2_MAGIC           0xEF53
#define EXT2_ROOT_INO        2
#define EXT2_BAD_BLOCK_INO   1
#define EXT2_GOOD_OLD_FIRST  11

#define EXT2_S_IFREG         0x8000
#define EXT2_S_IFDIR         0x4000
#define EXT2_S_IFLNK         0xA000

#define EXT2_N_BLOCKS        15

/* ===== Public API ===== */

int ext2_mount(void);

/* Filesystem operations table */
extern const struct filesystem_ops ext2_fs_ops;

#endif
