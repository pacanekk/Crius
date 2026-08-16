#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ext2_internal.h"
#include "mm/kmalloc.h"

/* ===== Inode number to group/index conversion ===== */

uint32_t ino_to_group(uint32_t ino) {
    return (ino - 1) / inodes_per_group;
}

uint32_t ino_to_index(uint32_t ino) {
    return (ino - 1) % inodes_per_group;
}

/* ===== Inode read/write ===== */

int ext2_read_inode(uint32_t ino, struct ext2_inode *out) {
    if (!mounted || ino == 0) return -1;

    uint32_t group = ino_to_group(ino);
    uint32_t index = ino_to_index(ino);
    if (group >= num_groups) return -1;

    uint32_t inode_table_block = bgds[group].bg_inode_table;
    uint32_t inode_offset = index * inode_size;
    uint32_t block_in_table = inode_offset / block_size;
    uint32_t offset_in_block = inode_offset % block_size;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -1;

    if (ext2_read_block(inode_table_block + block_in_table, buf) < 0) {
        kfree(buf);
        return -1;
    }

    memcpy(out, buf + offset_in_block, sizeof(struct ext2_inode));
    kfree(buf);
    return 0;
}

int ext2_write_inode(uint32_t ino, const struct ext2_inode *inode) {
    if (!mounted || ino == 0) return -1;

    uint32_t group = ino_to_group(ino);
    uint32_t index = ino_to_index(ino);
    if (group >= num_groups) return -1;

    uint32_t inode_table_block = bgds[group].bg_inode_table;
    uint32_t inode_offset = index * inode_size;
    uint32_t block_in_table = inode_offset / block_size;
    uint32_t offset_in_block = inode_offset % block_size;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -1;

    if (ext2_read_block(inode_table_block + block_in_table, buf) < 0) {
        kfree(buf);
        return -1;
    }

    memcpy(buf + offset_in_block, inode, sizeof(struct ext2_inode));
    int ret = ext2_write_block(inode_table_block + block_in_table, buf);
    kfree(buf);
    return ret;
}
