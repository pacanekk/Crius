#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ext2_internal.h"
#include "mm/kmalloc.h"

/* ===== Block bitmap allocation ===== */

uint32_t ext2_alloc_block(void) {
    if (!mounted) return 0;
    uint8_t *bitmap = kmalloc(block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        if (bgds[g].bg_free_blocks_count == 0) continue;
        if (ext2_read_block(bgds[g].bg_block_bitmap, bitmap) < 0) continue;

        for (uint32_t i = 0; i < block_size; i++) {
            if (bitmap[i] == 0xFF) continue;
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    ext2_write_block(bgds[g].bg_block_bitmap, bitmap);
                    bgds[g].bg_free_blocks_count--;
                    sb.s_free_blocks_count--;
                    kfree(bitmap);
                    ext2_sync_superblock();
                    ext2_sync_bgds();
                    return first_data_block + g * blocks_per_group + i * 8 + bit;
                }
            }
        }
    }
    kfree(bitmap);
    return 0;
}

int ext2_free_block(uint32_t block) {
    if (!mounted || block < first_data_block) return -1;
    uint32_t g = (block - first_data_block) / blocks_per_group;
    uint32_t idx = (block - first_data_block) % blocks_per_group;
    if (g >= num_groups) return -1;

    uint8_t *bitmap = kmalloc(block_size);
    if (!bitmap) return -1;
    if (ext2_read_block(bgds[g].bg_block_bitmap, bitmap) < 0) { kfree(bitmap); return -1; }

    uint32_t byte = idx / 8;
    uint8_t bit = idx % 8;
    bitmap[byte] &= ~(1 << bit);
    ext2_write_block(bgds[g].bg_block_bitmap, bitmap);
    bgds[g].bg_free_blocks_count++;
    sb.s_free_blocks_count++;
    kfree(bitmap);
    ext2_sync_superblock();
    ext2_sync_bgds();
    return 0;
}

/* ===== Inode bitmap allocation ===== */

uint32_t ext2_alloc_inode(int is_dir) {
    if (!mounted) return 0;
    uint8_t *bitmap = kmalloc(block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        if (bgds[g].bg_free_inodes_count == 0) continue;
        if (ext2_read_block(bgds[g].bg_inode_bitmap, bitmap) < 0) continue;

        for (uint32_t i = 0; i < block_size; i++) {
            if (bitmap[i] == 0xFF) continue;
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    ext2_write_block(bgds[g].bg_inode_bitmap, bitmap);
                    bgds[g].bg_free_inodes_count--;
                    if (is_dir) bgds[g].bg_used_dirs_count++;
                    sb.s_free_inodes_count--;
                    kfree(bitmap);
                    ext2_sync_superblock();
                    ext2_sync_bgds();
                    return g * inodes_per_group + i * 8 + bit + 1;
                }
            }
        }
    }
    kfree(bitmap);
    return 0;
}

int ext2_free_inode(uint32_t ino) {
    if (!mounted || ino == 0) return -1;
    uint32_t g = ino_to_group(ino);
    uint32_t idx = ino_to_index(ino);
    if (g >= num_groups) return -1;

    uint8_t *bitmap = kmalloc(block_size);
    if (!bitmap) return -1;
    if (ext2_read_block(bgds[g].bg_inode_bitmap, bitmap) < 0) { kfree(bitmap); return -1; }

    uint32_t byte = idx / 8;
    uint8_t bit = idx % 8;
    bitmap[byte] &= ~(1 << bit);
    ext2_write_block(bgds[g].bg_inode_bitmap, bitmap);
    bgds[g].bg_free_inodes_count++;
    sb.s_free_inodes_count++;
    kfree(bitmap);
    ext2_sync_superblock();
    ext2_sync_bgds();
    return 0;
}
