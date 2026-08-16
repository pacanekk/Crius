#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ext2_internal.h"
#include "mm/kmalloc.h"

/* ===== Block mapping helpers (direct, indirect, double-indirect, triple-indirect) ===== */

uint32_t get_block_from_inode(const struct ext2_inode *inode, uint32_t logical) {
    if (logical < 12) {
        return inode->i_block[logical];
    }

    uint32_t per_block = block_size / 4;
    logical -= 12;

    if (logical < per_block) {
        uint32_t *buf = kmalloc(block_size);
        if (!buf) return 0;
        if (ext2_read_block(inode->i_block[12], buf) < 0) { kfree(buf); return 0; }
        uint32_t result = buf[logical];
        kfree(buf);
        return result;
    }

    logical -= per_block;
    if (logical < per_block * per_block) {
        uint32_t *buf1 = kmalloc(block_size);
        uint32_t *buf2 = kmalloc(block_size);
        if (!buf1 || !buf2) { kfree(buf1); kfree(buf2); return 0; }
        if (ext2_read_block(inode->i_block[13], buf1) < 0) { kfree(buf1); kfree(buf2); return 0; }
        uint32_t idx1 = logical / per_block;
        uint32_t idx2 = logical % per_block;
        if (ext2_read_block(buf1[idx1], buf2) < 0) { kfree(buf1); kfree(buf2); return 0; }
        uint32_t result = buf2[idx2];
        kfree(buf1);
        kfree(buf2);
        return result;
    }

    logical -= per_block * per_block;
    if (logical < per_block * per_block * per_block) {
        uint32_t *b1 = kmalloc(block_size);
        uint32_t *b2 = kmalloc(block_size);
        uint32_t *b3 = kmalloc(block_size);
        if (!b1 || !b2 || !b3) { kfree(b1); kfree(b2); kfree(b3); return 0; }
        if (ext2_read_block(inode->i_block[14], b1) < 0) { kfree(b1); kfree(b2); kfree(b3); return 0; }
        uint32_t i1 = logical / (per_block * per_block);
        uint32_t rem = logical % (per_block * per_block);
        uint32_t i2 = rem / per_block;
        uint32_t i3 = rem % per_block;
        if (ext2_read_block(b1[i1], b2) < 0) { kfree(b1); kfree(b2); kfree(b3); return 0; }
        if (ext2_read_block(b2[i2], b3) < 0) { kfree(b1); kfree(b2); kfree(b3); return 0; }
        uint32_t result = b3[i3];
        kfree(b1); kfree(b2); kfree(b3);
        return result;
    }

    return 0;
}

int set_block_in_inode(struct ext2_inode *inode, uint32_t logical, uint32_t phys) {
    if (logical < 12) {
        inode->i_block[logical] = phys;
        return 0;
    }

    uint32_t per_block = block_size / 4;
    logical -= 12;
    uint32_t *buf = kmalloc(block_size);
    if (!buf) return -1;

    if (logical < per_block) {
        if (inode->i_block[12] == 0) {
            memset(buf, 0, block_size);
            inode->i_block[12] = ext2_alloc_block();
            if (inode->i_block[12] == 0) { kfree(buf); return -1; }
        } else {
            ext2_read_block(inode->i_block[12], buf);
        }
        buf[logical] = phys;
        ext2_write_block(inode->i_block[12], buf);
        kfree(buf);
        return 0;
    }

    kfree(buf);
    return -1;
}

int ext2_free_inode_data(struct ext2_inode *inode) {
    uint32_t per_block = block_size / 4;
    uint32_t dir_blocks = (inode->i_size + block_size - 1) / block_size;
    if (dir_blocks == 0) dir_blocks = inode->i_blocks / (block_size / 512);

    for (uint32_t logical = 0; logical < dir_blocks; logical++) {
        uint32_t phys = get_block_from_inode(inode, logical);
        if (phys != 0) {
            ext2_free_block(phys);
        }
    }

    if (inode->i_block[12] != 0) {
        uint32_t *buf = kmalloc(block_size);
        if (buf) {
            if (ext2_read_block(inode->i_block[12], buf) >= 0) {
                for (uint32_t i = 0; i < per_block; i++) {
                    if (buf[i] != 0) ext2_free_block(buf[i]);
                }
            }
            kfree(buf);
        }
        ext2_free_block(inode->i_block[12]);
    }

    memset(inode->i_block, 0, sizeof(inode->i_block));
    inode->i_blocks = 0;
    inode->i_size = 0;
    return 0;
}
