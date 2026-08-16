#ifndef EXT2_INTERNAL_H
#define EXT2_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "fs/ext2.h"
#include "drivers/block_device.h"

/* ===== Private on-disk structures ===== */

struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    uint8_t  s_volume_name[16];
    uint8_t  s_last_mounted[64];
    uint32_t s_algo_bitmap;
} __attribute__((packed));

struct ext2_bgd {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} __attribute__((packed));

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint8_t  i_faddr[12];
} __attribute__((packed));

struct ext2_dirent {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

/* ===== Shared global state (defined in ext2.c) ===== */

extern int mounted;
extern struct block_device *cur_bd;
extern uint32_t block_size;
extern uint32_t blocks_per_group;
extern uint32_t inodes_per_group;
extern uint32_t inode_size;
extern uint32_t num_groups;
extern uint32_t first_data_block;
extern struct ext2_superblock sb;
extern struct ext2_bgd *bgds;

/* ===== Mount context (for multi-mount support) ===== */

struct ext2_mount_ctx {
    int mounted;
    struct block_device *cur_bd;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t num_groups;
    uint32_t first_data_block;
    struct ext2_superblock sb;
    struct ext2_bgd *bgds;
};
void ext2_save_ctx(struct ext2_mount_ctx *ctx);
void ext2_restore_ctx(struct ext2_mount_ctx *ctx);

/* ===== Private block I/O (ext2.c) ===== */

uint32_t sec_per_block(void);
int ext2_read_sectors_off(uint64_t lba, size_t count, void *buf);
int ext2_write_sectors_off(uint64_t lba, size_t count, const void *buf);
int ext2_read_block(uint32_t block, void *buf);
int ext2_write_block(uint32_t block, const void *buf);

/* ===== Private block mapping (ext2_block.c) ===== */

uint32_t get_block_from_inode(const struct ext2_inode *inode, uint32_t logical);
int set_block_in_inode(struct ext2_inode *inode, uint32_t logical, uint32_t phys);
int ext2_free_inode_data(struct ext2_inode *inode);

/* ===== Private inode operations (ext2_inode.c) ===== */

uint32_t ino_to_group(uint32_t ino);
uint32_t ino_to_index(uint32_t ino);
int ext2_read_inode(uint32_t ino, struct ext2_inode *out);
int ext2_write_inode(uint32_t ino, const struct ext2_inode *inode);

/* ===== Private allocation (ext2_alloc.c) ===== */

uint32_t ext2_alloc_block(void);
int ext2_free_block(uint32_t block);
uint32_t ext2_alloc_inode(int is_dir);
int ext2_free_inode(uint32_t ino);

/* ===== Private directory operations (ext2_dir.c) ===== */

int local_memcmp(const void *a, const void *b, size_t n);
int ext2_list_dir(uint32_t ino, void (*cb)(const char *name, uint32_t ino, uint8_t type, void *ctx), void *ctx);
int ext2_lookup(uint32_t dir_ino, const char *name, uint32_t *out_ino);
int add_dirent(uint32_t dir_ino, uint32_t child_ino, const char *name, uint8_t type);
int ext2_create_file(uint32_t dir_ino, const char *name, uint16_t mode);
int ext2_mkdir(uint32_t dir_ino, const char *name);
int ext2_unlink(uint32_t dir_ino, const char *name);

/* ===== Private file data operations (ext2_file.c) ===== */

int ext2_read_file(uint32_t ino, void *buf, size_t bufsize);
int ext2_write_file(uint32_t ino, const void *buf, size_t len);
int ext2_append_file(uint32_t ino, const void *buf, size_t len);
int ext2_write_at(uint32_t ino, size_t offset, const void *buf, size_t len);
int ext2_read_at(uint32_t ino, size_t offset, void *buf, size_t count);

extern const struct file_operations ext2_file_ops;
extern const struct file_operations ext2_dir_ops;

/* ===== Private path resolution (ext2_ops.c) ===== */

int ext2_resolve_path(const char *path, uint32_t *out_ino);
int ext2_split_path(const char *path, char *parent, char *fname);

/* ===== Private mount helpers (ext2.c) ===== */

int ext2_do_mount(struct block_device *bd);
void ext2_sync_superblock(void);
void ext2_sync_bgds(void);
int ext2_umount(void);

/* ===== Internal query functions (used by ext2_fs_info) ===== */

int ext2_is_mounted(void);
uint32_t ext2_get_root_ino(void);
uint32_t ext2_get_block_size(void);
uint32_t ext2_get_total_blocks(void);
uint32_t ext2_get_free_blocks(void);
uint32_t ext2_get_total_inodes(void);
uint32_t ext2_get_free_inodes(void);

#endif
