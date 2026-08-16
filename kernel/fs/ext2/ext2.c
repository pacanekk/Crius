#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs/ext2.h"
#include "ext2_internal.h"
#include "mm/kmalloc.h"
#include "drivers/serial.h"
#include "fs/vfs.h"

/* ===== Shared global state ===== */
int mounted = 0;
struct block_device *cur_bd = NULL;
uint32_t block_size;
uint32_t blocks_per_group;
uint32_t inodes_per_group;
uint32_t inode_size;
uint32_t num_groups;
uint32_t first_data_block;
struct ext2_superblock sb;
struct ext2_bgd *bgds;

/* ===== Mount context (stored as fs_private) ===== */

/* Save current globals into a context struct */
void ext2_save_ctx(struct ext2_mount_ctx *ctx) {
    ctx->mounted = mounted;
    ctx->cur_bd = cur_bd;
    ctx->block_size = block_size;
    ctx->blocks_per_group = blocks_per_group;
    ctx->inodes_per_group = inodes_per_group;
    ctx->inode_size = inode_size;
    ctx->num_groups = num_groups;
    ctx->first_data_block = first_data_block;
    memcpy(&ctx->sb, &sb, sizeof(sb));
    ctx->bgds = bgds;
}

/* Restore globals from a context struct */
void ext2_restore_ctx(struct ext2_mount_ctx *ctx) {
    mounted = ctx->mounted;
    cur_bd = ctx->cur_bd;
    block_size = ctx->block_size;
    blocks_per_group = ctx->blocks_per_group;
    inodes_per_group = ctx->inodes_per_group;
    inode_size = ctx->inode_size;
    num_groups = ctx->num_groups;
    first_data_block = ctx->first_data_block;
    memcpy(&sb, &ctx->sb, sizeof(sb));
    bgds = ctx->bgds;
}

/* ===== Block I/O helpers ===== */

uint32_t sec_per_block(void) {
    return block_size / 512;
}

int ext2_read_sectors_off(uint64_t lba, size_t count, void *buf) {
    if (!cur_bd) return -1;
    return cur_bd->read(cur_bd, lba, (uint32_t)count, buf);
}

int ext2_write_sectors_off(uint64_t lba, size_t count, const void *buf) {
    if (!cur_bd) return -1;
    return cur_bd->write(cur_bd, lba, (uint32_t)count, buf);
}

int ext2_read_block(uint32_t block, void *buf) {
    uint64_t lba = (uint64_t)block * sec_per_block();
    return ext2_read_sectors_off(lba, sec_per_block(), buf);
}

int ext2_write_block(uint32_t block, const void *buf) {
    uint64_t lba = (uint64_t)block * sec_per_block();
    return ext2_write_sectors_off(lba, sec_per_block(), buf);
}

/* ===== Query functions ===== */

int ext2_is_mounted(void) { return mounted; }
uint32_t ext2_get_root_ino(void) { return EXT2_ROOT_INO; }
uint32_t ext2_get_block_size(void) { return block_size; }
uint32_t ext2_get_total_blocks(void) { return sb.s_blocks_count; }
uint32_t ext2_get_free_blocks(void) { return sb.s_free_blocks_count; }
uint32_t ext2_get_total_inodes(void) { return sb.s_inodes_count; }
uint32_t ext2_get_free_inodes(void) { return sb.s_free_inodes_count; }

/* ===== Core mount logic (shared by ext2_mount and ext2_fs_mount) ===== */

int ext2_do_mount(struct block_device *bd) {
    if (mounted) return -1;
    if (!bd) return -1;
    cur_bd = bd;

    uint8_t *sbuf = kmalloc(1024);
    if (!sbuf) { cur_bd = NULL; return -1; }

    if (ext2_read_sectors_off(2, 2, sbuf) < 0) {
        kfree(sbuf);
        cur_bd = NULL;
        return -1;
    }

    memcpy(&sb, sbuf + (EXT2_SUPER_OFFSET - 1024), sizeof(sb));
    kfree(sbuf);

    if (sb.s_magic != EXT2_MAGIC) {
        cur_bd = NULL;
        return -1;
    }

    block_size = 1024 << sb.s_log_block_size;
    blocks_per_group = sb.s_blocks_per_group;
    inodes_per_group = sb.s_inodes_per_group;
    inode_size = sb.s_inode_size ? sb.s_inode_size : 128;
    first_data_block = sb.s_first_data_block;
    num_groups = (sb.s_blocks_count - first_data_block + blocks_per_group - 1) / blocks_per_group;

    uint32_t bgd_size = num_groups * sizeof(struct ext2_bgd);
    bgds = kmalloc(bgd_size);
    if (!bgds) { cur_bd = NULL; return -1; }

    uint32_t bgd_block = first_data_block + 1;
    uint8_t *bbuf = kmalloc(block_size);
    if (!bbuf) { kfree(bgds); cur_bd = NULL; return -1; }

    uint32_t remaining = bgd_size;
    uint32_t offset = 0;
    while (remaining > 0) {
        if (ext2_read_block(bgd_block, bbuf) < 0) {
            serial_puts("ext2: cannot read BGD\n");
            kfree(bbuf);
            kfree(bgds);
            cur_bd = NULL;
            return -1;
        }
        uint32_t chunk = remaining < block_size ? remaining : block_size;
        memcpy((uint8_t *)bgds + offset, bbuf, chunk);
        offset += chunk;
        remaining -= chunk;
        bgd_block++;
    }
    kfree(bbuf);

    mounted = 1;
    return 0;
}

int ext2_umount(void) {
    if (!mounted) return -1;
    mounted = 0;
    cur_bd = NULL;
    if (bgds) {
        kfree(bgds);
        bgds = NULL;
    }
    return 0;
}

int ext2_mount(void) {
    struct block_device *bd = block_device_find("hda");
    if (!bd) return -1;
    return ext2_do_mount(bd);
}

void ext2_sync_superblock(void) {
    if (!mounted) return;
    uint8_t *buf = kmalloc(1024);
    if (!buf) return;
    if (ext2_read_sectors_off(2, 2, buf) < 0) {
        kfree(buf);
        return;
    }
    memcpy(buf + (EXT2_SUPER_OFFSET - 1024), &sb, sizeof(sb));
    ext2_write_sectors_off(2, 2, buf);
    kfree(buf);
}

void ext2_sync_bgds(void) {
    if (!mounted || !bgds) return;
    uint32_t bgd_size = num_groups * sizeof(struct ext2_bgd);
    uint32_t bgd_block = first_data_block + 1;
    uint8_t *buf = kmalloc(block_size);
    if (!buf) return;

    uint32_t remaining = bgd_size;
    uint32_t offset = 0;
    while (remaining > 0) {
        uint32_t chunk = remaining < block_size ? remaining : block_size;
        if (ext2_read_block(bgd_block, buf) < 0) break;
        memcpy(buf, (uint8_t *)bgds + offset, chunk);
        ext2_write_block(bgd_block, buf);
        offset += chunk;
        remaining -= chunk;
        bgd_block++;
    }
    kfree(buf);
}
