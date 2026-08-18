#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ext2_internal.h"
#include "mm/kmalloc.h"

int local_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = a, *pb = b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

/* ===== Directory listing ===== */

int ext2_list_dir(uint32_t ino, void (*cb)(const char *name, uint32_t ino, uint8_t type, void *ctx), void *ctx) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFDIR) return -ENOENT;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -ENOENT;

    uint32_t dir_blocks = (inode.i_size + block_size - 1) / block_size;
    for (uint32_t logical = 0; logical < dir_blocks; logical++) {
        uint32_t phys = get_block_from_inode(&inode, logical);
        if (phys == 0) break;

        if (ext2_read_block(phys, buf) < 0) {
            kfree(buf);
            return -ENOENT;
        }

        uint32_t offset = 0;
        while (offset < block_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(buf + offset);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                char name[256];
                uint32_t nlen = de->name_len;
                if (nlen >= 255) nlen = 254;
                memcpy(name, de->name, nlen);
                name[nlen] = '\0';
                cb(name, de->inode, de->file_type, ctx);
            }
            offset += de->rec_len;
        }
    }

    kfree(buf);
    return 0;
}

/* ===== Directory lookup ===== */

int ext2_lookup(uint32_t dir_ino, const char *name, uint32_t *out_ino) {
    struct ext2_inode inode;
    if (ext2_read_inode(dir_ino, &inode) < 0) return -ENOENT;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFDIR) return -ENOENT;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -ENOENT;

    uint32_t dir_blocks = (inode.i_size + block_size - 1) / block_size;
    for (uint32_t logical = 0; logical < dir_blocks; logical++) {
        uint32_t phys = get_block_from_inode(&inode, logical);
        if (phys == 0) break;

        if (ext2_read_block(phys, buf) < 0) {
            kfree(buf);
            return -ENOENT;
        }

        uint32_t offset = 0;
        while (offset < block_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(buf + offset);
            if (de->rec_len == 0) break;
            if (de->inode != 0) {
                uint32_t nlen = de->name_len;
                if (nlen == strlen(name) && local_memcmp(de->name, name, nlen) == 0) {
                    *out_ino = de->inode;
                    kfree(buf);
                    return 0;
                }
            }
            offset += de->rec_len;
        }
    }

    kfree(buf);
    return -ENOENT;
}

/* ===== Add directory entry ===== */

int add_dirent(uint32_t dir_ino, uint32_t child_ino, const char *name, uint8_t type) {
    struct ext2_inode dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) < 0) return -ENOENT;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -ENOENT;

    uint32_t name_len = strlen(name);
    uint32_t new_reclen = ((8 + name_len + 3) & ~3);

    uint32_t logical = 0;
    size_t remaining = dir_inode.i_size;

    while (remaining > 0) {
        uint32_t phys = get_block_from_inode(&dir_inode, logical);
        if (phys == 0) break;
        if (ext2_read_block(phys, buf) < 0) {
            kfree(buf);
            return -ENOENT;
        }

        uint32_t offset = 0;
        while (offset < block_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(buf + offset);
            if (de->rec_len == 0) break;

            uint32_t actual_len = ((8 + de->name_len + 3) & ~3);
            if (de->inode == 0 && de->rec_len >= new_reclen) {
                de->inode = child_ino;
                de->name_len = name_len;
                de->file_type = type;
                memcpy(de->name, name, name_len);
                ext2_write_block(phys, buf);
                kfree(buf);
                return 0;
            }
            if (de->rec_len > actual_len + new_reclen) {
                uint32_t old_reclen = de->rec_len;
                de->rec_len = actual_len;
                struct ext2_dirent *new_de = (struct ext2_dirent *)(buf + offset + actual_len);
                new_de->inode = child_ino;
                new_de->rec_len = old_reclen - actual_len;
                new_de->name_len = name_len;
                new_de->file_type = type;
                memcpy(new_de->name, name, name_len);
                ext2_write_block(phys, buf);
                kfree(buf);
                return 0;
            }
            offset += de->rec_len;
        }
        remaining -= block_size;
        logical++;
    }

    uint32_t new_block = ext2_alloc_block();
    if (new_block == 0) { kfree(buf); return -ENOENT; }
    memset(buf, 0, block_size);
    struct ext2_dirent *de = (struct ext2_dirent *)buf;
    de->inode = child_ino;
    de->rec_len = block_size;
    de->name_len = name_len;
    de->file_type = type;
    memcpy(de->name, name, name_len);
    ext2_write_block(new_block, buf);

    set_block_in_inode(&dir_inode, logical, new_block);
    dir_inode.i_size += block_size;
    dir_inode.i_blocks += (block_size / 512);
    ext2_write_inode(dir_ino, &dir_inode);

    kfree(buf);
    return 0;
}

/* ===== Create file ===== */

int ext2_create_file(uint32_t dir_ino, const char *name, uint16_t mode) {
    uint32_t existing;
    if (ext2_lookup(dir_ino, name, &existing) == 0) return -ENOENT;

    uint32_t new_ino = ext2_alloc_inode(0);
    if (new_ino == 0) return -ENOENT;

    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = EXT2_S_IFREG | (mode & 0xFFF);
    inode.i_links_count = 1;
    inode.i_size = 0;
    inode.i_blocks = 0;
    ext2_write_inode(new_ino, &inode);

    if (add_dirent(dir_ino, new_ino, name, 1) < 0) {
        ext2_free_inode(new_ino);
        return -ENOENT;
    }
    return 0;
}

/* ===== Make directory ===== */

int ext2_mkdir(uint32_t dir_ino, const char *name) {
    uint32_t existing;
    if (ext2_lookup(dir_ino, name, &existing) == 0) return -ENOENT;

    uint32_t new_ino = ext2_alloc_inode(1);
    if (new_ino == 0) return -ENOENT;

    uint32_t new_block = ext2_alloc_block();
    if (new_block == 0) { ext2_free_inode(new_ino); return -ENOENT; }

    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = EXT2_S_IFDIR | 0755;
    inode.i_links_count = 2;
    inode.i_size = block_size;
    inode.i_blocks = block_size / 512;
    inode.i_block[0] = new_block;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) { ext2_free_inode(new_ino); return -ENOENT; }
    memset(buf, 0, block_size);

    struct ext2_dirent *dot = (struct ext2_dirent *)buf;
    dot->inode = new_ino;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = 2;
    dot->name[0] = '.';

    struct ext2_dirent *dotdot = (struct ext2_dirent *)(buf + 12);
    dotdot->inode = dir_ino;
    dotdot->rec_len = block_size - 12;
    dotdot->name_len = 2;
    dotdot->file_type = 2;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    ext2_write_block(new_block, buf);
    kfree(buf);

    ext2_write_inode(new_ino, &inode);

    if (add_dirent(dir_ino, new_ino, name, 2) < 0) {
        ext2_free_inode(new_ino);
        ext2_free_block(new_block);
        return -ENOENT;
    }

    struct ext2_inode parent_inode;
    ext2_read_inode(dir_ino, &parent_inode);
    parent_inode.i_links_count++;
    ext2_write_inode(dir_ino, &parent_inode);

    return 0;
}

/* ===== Unlink ===== */

int ext2_unlink(uint32_t dir_ino, const char *name) {
    uint32_t ino;
    if (ext2_lookup(dir_ino, name, &ino) < 0) return -ENOENT;

    struct ext2_inode dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) < 0) return -ENOENT;

    uint8_t *buf = kmalloc(block_size);
    if (!buf) return -ENOENT;

    uint32_t dir_blocks = (dir_inode.i_size + block_size - 1) / block_size;
    for (uint32_t b = 0; b < dir_blocks; b++) {
        uint32_t phys = get_block_from_inode(&dir_inode, b);
        if (phys == 0) continue;
        ext2_read_block(phys, buf);

        uint32_t pos = 0;
        struct ext2_dirent *prev = NULL;
        while (pos < block_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(buf + pos);
            if (de->rec_len == 0) break;

            if (de->inode == ino) {
                if (prev) {
                    prev->rec_len += de->rec_len;
                } else {
                    de->inode = 0;
                }
                ext2_write_block(phys, buf);
                kfree(buf);

                struct ext2_inode file_inode;
                if (ext2_read_inode(ino, &file_inode) == 0) {
                    file_inode.i_links_count--;
                    if (file_inode.i_links_count == 0) {
                        ext2_free_inode_data(&file_inode);
                        ext2_write_inode(ino, &file_inode);
                        ext2_free_inode(ino);
                    } else {
                        ext2_write_inode(ino, &file_inode);
                    }
                }
                return 0;
            }
            prev = de;
            pos += de->rec_len;
        }
    }

    kfree(buf);
    return -ENOENT;
}
