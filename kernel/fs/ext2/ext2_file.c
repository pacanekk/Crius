#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ext2_internal.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"

/* ===== Read entire file ===== */

int ext2_read_file(uint32_t ino, void *buf, size_t bufsize) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;

    if ((inode.i_mode & 0xF000) == EXT2_S_IFLNK) {
        if (inode.i_size < 60) {
            const char *target = (const char *)inode.i_block;
            size_t len = inode.i_size < bufsize ? inode.i_size : bufsize;
            memcpy(buf, target, len);
            return (int)len;
        }
    }

    if ((inode.i_mode & 0xF000) != EXT2_S_IFREG) return -ENOENT;

    size_t remaining = inode.i_size < bufsize ? inode.i_size : bufsize;
    size_t pos = 0;
    uint32_t logical = 0;
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -ENOENT;

    while (remaining > 0) {
        uint32_t phys = get_block_from_inode(&inode, logical);
        if (phys == 0) break;

        if (ext2_read_block(phys, block_buf) < 0) {
            kfree(block_buf);
            return -ENOENT;
        }

        size_t chunk = remaining < block_size ? remaining : block_size;
        memcpy((uint8_t *)buf + pos, block_buf, chunk);
        pos += chunk;
        remaining -= chunk;
        logical++;
    }

    kfree(block_buf);
    return (int)pos;
}

/* ===== Write entire file ===== */

int ext2_write_file(uint32_t ino, const void *buf, size_t len) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFREG) return -ENOENT;

    uint32_t blocks_needed = (len + block_size - 1) / block_size;
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -ENOENT;

    size_t pos = 0;
    for (uint32_t i = 0; i < blocks_needed; i++) {
        size_t chunk = (len - pos) < block_size ? (len - pos) : block_size;
        memset(block_buf, 0, block_size);
        memcpy(block_buf, (const uint8_t *)buf + pos, chunk);

        uint32_t phys = get_block_from_inode(&inode, i);
        if (phys == 0) {
            phys = ext2_alloc_block();
            if (phys == 0) { kfree(block_buf); return -ENOENT; }
            set_block_in_inode(&inode, i, phys);
        }
        ext2_write_block(phys, block_buf);
        pos += chunk;
    }

    inode.i_size = (uint32_t)len;
    inode.i_blocks = blocks_needed * (block_size / 512);
    inode.i_mtime = 0;
    int ret = ext2_write_inode(ino, &inode);
    kfree(block_buf);
    return ret;
}

/* ===== Append to file ===== */

int ext2_append_file(uint32_t ino, const void *buf, size_t len) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFREG) return -ENOENT;

    uint32_t existing = inode.i_size;
    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -ENOENT;

    size_t pos = 0;
    uint32_t offset = existing;
    while (pos < len) {
        uint32_t logical = offset / block_size;
        uint32_t off_in_blk = offset % block_size;
        size_t chunk = block_size - off_in_blk;
        if (chunk > len - pos) chunk = len - pos;

        uint32_t phys = get_block_from_inode(&inode, logical);
        if (phys == 0) {
            phys = ext2_alloc_block();
            if (phys == 0) { kfree(block_buf); return -ENOENT; }
            memset(block_buf, 0, block_size);
            set_block_in_inode(&inode, logical, phys);
        } else {
            if (off_in_blk > 0 || chunk < block_size) {
                ext2_read_block(phys, block_buf);
            } else {
                memset(block_buf, 0, block_size);
            }
        }

        memcpy(block_buf + off_in_blk, (const uint8_t *)buf + pos, chunk);
        ext2_write_block(phys, block_buf);
        pos += chunk;
        offset += chunk;
    }

    inode.i_size = existing + (uint32_t)len;
    uint32_t blocks_needed = (inode.i_size + block_size - 1) / block_size;
    inode.i_blocks = blocks_needed * (block_size / 512);
    int ret = ext2_write_inode(ino, &inode);
    kfree(block_buf);
    return ret;
}

/* ===== Write at offset ===== */

int ext2_write_at(uint32_t ino, size_t offset, const void *buf, size_t len) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFREG) return -ENOENT;

    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -ENOENT;

    size_t pos = 0;
    size_t file_offset = offset;
    while (pos < len) {
        uint32_t logical = (uint32_t)(file_offset / block_size);
        uint32_t off_in_blk = (uint32_t)(file_offset % block_size);
        size_t chunk = block_size - off_in_blk;
        if (chunk > len - pos) chunk = len - pos;

        uint32_t phys = get_block_from_inode(&inode, logical);
        if (phys == 0) {
            phys = ext2_alloc_block();
            if (phys == 0) { kfree(block_buf); return -ENOENT; }
            memset(block_buf, 0, block_size);
            set_block_in_inode(&inode, logical, phys);
        } else {
            if (off_in_blk > 0 || chunk < block_size) {
                ext2_read_block(phys, block_buf);
            } else {
                memset(block_buf, 0, block_size);
            }
        }

        memcpy(block_buf + off_in_blk, (const uint8_t *)buf + pos, chunk);
        ext2_write_block(phys, block_buf);
        pos += chunk;
        file_offset += chunk;
    }

    if (file_offset > inode.i_size)
        inode.i_size = (uint32_t)file_offset;
    uint32_t blocks_needed = (inode.i_size + block_size - 1) / block_size;
    inode.i_blocks = blocks_needed * (block_size / 512);
    int ret = ext2_write_inode(ino, &inode);
    kfree(block_buf);
    return ret;
}

/* ===== Read at offset ===== */

int ext2_read_at(uint32_t ino, size_t offset, void *buf, size_t count) {
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFREG) return -ENOENT;

    if (offset >= inode.i_size) return 0;
    size_t avail = inode.i_size - offset;
    if (count > avail) count = avail;

    uint8_t *block_buf = kmalloc(block_size);
    if (!block_buf) return -ENOENT;

    size_t pos = 0;
    size_t file_offset = offset;
    while (pos < count) {
        uint32_t logical = (uint32_t)(file_offset / block_size);
        uint32_t off_in_blk = (uint32_t)(file_offset % block_size);
        size_t chunk = block_size - off_in_blk;
        if (chunk > count - pos) chunk = count - pos;

        uint32_t phys = get_block_from_inode(&inode, logical);
        if (phys == 0) break;

        if (ext2_read_block(phys, block_buf) < 0) {
            kfree(block_buf);
            return -ENOENT;
        }

        memcpy((uint8_t *)buf + pos, block_buf + off_in_blk, chunk);
        pos += chunk;
        file_offset += chunk;
    }

    kfree(block_buf);
    return (int)pos;
}

/* ===== File operations vtable ===== */

static int ext2_file_read(struct file *f, char *buf, int count) {
    uint32_t ino = (uint32_t)(uintptr_t)f->priv;
    int n = ext2_read_at(ino, f->offset, buf, count);
    if (n <= 0) return 0;
    f->offset += n;
    return n;
}

static int ext2_file_write(struct file *f, const char *buf, int count) {
    uint32_t ino = (uint32_t)(uintptr_t)f->priv;
    size_t write_offset = f->offset;
    if (f->flags & O_APPEND) {
        struct ext2_inode inode;
        if (ext2_read_inode(ino, &inode) < 0) return -ENOENT;
        write_offset = inode.i_size;
    }
    int ret = ext2_write_at(ino, write_offset, buf, count);
    if (ret >= 0) f->offset = write_offset + count;
    return ret;
}

const struct file_operations ext2_file_ops = {
    .read    = ext2_file_read,
    .write   = ext2_file_write,
    .ioctl   = NULL,
    .readdir = NULL,
    .close   = NULL,
};

const struct file_operations ext2_dir_ops = {
    .read    = NULL,
    .write   = NULL,
    .ioctl   = NULL,
    .readdir = vfs_dir_readdir,
    .close   = vfs_dir_close,
};
