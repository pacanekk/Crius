#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs/ext2.h"
#include "ext2_internal.h"
#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "drivers/serial.h"

/* ===== Path resolution ===== */

int ext2_resolve_path(const char *path, uint32_t *out_ino) {
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        *out_ino = ext2_get_root_ino();
        return 0;
    }

    uint32_t cur = ext2_get_root_ino();
    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        char name[256];
        int i = 0;
        while (*p && *p != '/' && i < 255) name[i++] = *p++;
        name[i] = '\0';
        if (*p == '/') p++;

        if (strcmp(name, ".") == 0) continue;
        if (strcmp(name, "..") == 0) {
            uint32_t parent;
            if (ext2_lookup(cur, "..", &parent) == 0)
                cur = parent;
            continue;
        }

        uint32_t next;
        if (ext2_lookup(cur, name, &next) != 0) return -1;
        cur = next;
    }
    *out_ino = cur;
    return 0;
}

int ext2_split_path(const char *path, char *parent, char *fname) {
    const char *slash = NULL;
    const char *p = path;
    if (*p == '/') p++;
    const char *name_start = p;
    for (; *p; p++) if (*p == '/') slash = p;

    if (slash) {
        int plen = slash - path;
        if (plen >= 255) return -1;
        memcpy(parent, path, plen);
        parent[plen] = '\0';
        if (parent[plen - 1] == '/' && plen > 1) parent[plen - 1] = '\0';
        if (parent[0] == '\0') { parent[0] = '/'; parent[1] = '\0'; }

        int flen = strlen(slash + 1);
        if (flen >= 255) return -1;
        memcpy(fname, slash + 1, flen + 1);
    } else {
        parent[0] = '/'; parent[1] = '\0';
        int flen = strlen(name_start);
        if (flen >= 255) return -1;
        memcpy(fname, name_start, flen + 1);
    }
    return 0;
}

/* ===== Filesystem operations vtable ===== */

static int ext2_fs_mount(struct block_device *device, void **fs_private) {
    int ret = ext2_do_mount(device);
    if (ret != 0) return ret;
    struct ext2_mount_ctx *ctx = kmalloc(sizeof(struct ext2_mount_ctx));
    if (!ctx) { ext2_umount(); return -1; }
    ext2_save_ctx(ctx);
    *fs_private = ctx;
    return 0;
}

static int ext2_fs_open(void *fs_private, const char *path, int flags, struct file *file) {
    ext2_restore_ctx(fs_private);
    uint32_t ino;
    if (ext2_resolve_path(path, &ino) < 0) {
        if (flags & O_CREAT) {
            char parent[256], fname[256];
            if (ext2_split_path(path, parent, fname) < 0) return -1;
            uint32_t parent_ino;
            if (ext2_resolve_path(parent, &parent_ino) < 0) return -1;
            if (ext2_create_file(parent_ino, fname, 0644) < 0) return -1;
            if (ext2_resolve_path(path, &ino) < 0) return -1;
        } else {
            return -1;
        }
    }

    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if ((flags & O_TRUNC) && (inode.i_mode & 0xF000) == EXT2_S_IFREG) {
        inode.i_size = 0;
        inode.i_blocks = 0;
        ext2_write_inode(ino, &inode);
    }

    file->priv = (void *)(uintptr_t)ino;
    if ((inode.i_mode & 0xF000) == EXT2_S_IFDIR)
        file->ops = &ext2_dir_ops;
    else
        file->ops = &ext2_file_ops;

    return 0;
}

static int ext2_fs_stat(void *fs_private, const char *path, int *type, size_t *size) {
    ext2_restore_ctx(fs_private);
    uint32_t ino;
    if (ext2_resolve_path(path, &ino) < 0) return -1;
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;
    int t = VFS_TYPE_FILE;
    if ((inode.i_mode & 0xF000) == EXT2_S_IFDIR) t = VFS_TYPE_DIR;
    if (type) *type = t;
    if (size) *size = inode.i_size;
    return 0;
}

static int ext2_fs_create(void *fs_private, const char *path) {
    ext2_restore_ctx(fs_private);
    char parent[256], fname[256];
    if (ext2_split_path(path, parent, fname) < 0) return -1;
    uint32_t parent_ino;
    if (ext2_resolve_path(parent, &parent_ino) < 0) return -1;
    return ext2_create_file(parent_ino, fname, 0644);
}

static int ext2_fs_mkdir(void *fs_private, const char *path) {
    ext2_restore_ctx(fs_private);
    char parent[256], fname[256];
    if (ext2_split_path(path, parent, fname) < 0) return -1;
    uint32_t parent_ino;
    if (ext2_resolve_path(parent, &parent_ino) < 0) return -1;
    return ext2_mkdir(parent_ino, fname);
}

static int ext2_fs_unlink(void *fs_private, const char *path) {
    ext2_restore_ctx(fs_private);
    char parent[256], fname[256];
    if (ext2_split_path(path, parent, fname) < 0) return -1;
    uint32_t parent_ino;
    if (ext2_resolve_path(parent, &parent_ino) < 0) return -1;
    return ext2_unlink(parent_ino, fname);
}

static int ext2_fs_read_at(void *fs_private, const char *path, uint64_t offset, void *buffer, size_t size) {
    ext2_restore_ctx(fs_private);
    uint32_t ino;
    if (ext2_resolve_path(path, &ino) < 0) return -1;
    return ext2_read_at(ino, (size_t)offset, buffer, size);
}

static int ext2_fs_write_at(void *fs_private, const char *path, uint64_t offset, const void *buffer, size_t size) {
    ext2_restore_ctx(fs_private);
    uint32_t ino;
    if (ext2_resolve_path(path, &ino) < 0) {
        char parent[256], fname[256];
        if (ext2_split_path(path, parent, fname) < 0) return -1;
        uint32_t parent_ino;
        if (ext2_resolve_path(parent, &parent_ino) < 0) return -1;
        if (ext2_create_file(parent_ino, fname, 0644) < 0) return -1;
        if (ext2_resolve_path(path, &ino) < 0) return -1;
    }
    return ext2_write_at(ino, (size_t)offset, buffer, size);
}

static int ext2_fs_truncate(void *fs_private, const char *path) {
    ext2_restore_ctx(fs_private);
    uint32_t ino;
    if (ext2_resolve_path(path, &ino) < 0) return -1;
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;
    inode.i_size = 0;
    inode.i_blocks = 0;
    return ext2_write_inode(ino, &inode);
}

/* ===== Readdir ===== */

struct ext2_readdir_ctx {
    uint64_t target;
    uint64_t current;
    int found;
    struct dirent *out;
};

static void ext2_readdir_pick(const char *name, uint32_t ino, uint8_t type, void *ctx_ptr) {
    struct ext2_readdir_ctx *ctx = (struct ext2_readdir_ctx *)ctx_ptr;
    if (!ctx || ctx->found) return;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
        return;
    if (ctx->current == ctx->target) {
        ctx->out->inode = ino;
        ctx->out->type = (type == 2) ? FILE_TYPE_DIR : FILE_TYPE_FILE;
        int i = 0;
        while (name[i] && i < 255) { ctx->out->name[i] = name[i]; i++; }
        ctx->out->name[i] = '\0';
        ctx->found = 1;
    }
    ctx->current++;
}

static int ext2_fs_readdir(void *fs_private, const char *path, uint64_t index, struct dirent *out) {
    ext2_restore_ctx(fs_private);
    uint32_t ino;
    if (ext2_resolve_path(path, &ino) < 0) return -1;
    struct ext2_inode inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFDIR) return -1;

    struct ext2_readdir_ctx ctx;
    ctx.target = index;
    ctx.current = 0;
    ctx.found = 0;
    ctx.out = out;

    ext2_list_dir(ino, ext2_readdir_pick, &ctx);

    return ctx.found ? 1 : 0;
}

static int ext2_fs_info(void *fs_private, struct vfs_fs_info *out) {
    ext2_restore_ctx(fs_private);
    if (!ext2_is_mounted()) {
        out->mounted = 0;
        return 0;
    }
    out->mounted = 1;
    out->block_size = ext2_get_block_size();
    out->total_blocks = ext2_get_total_blocks();
    out->free_blocks = ext2_get_free_blocks();
    out->total_inodes = ext2_get_total_inodes();
    out->free_inodes = ext2_get_free_inodes();
    return 0;
}

static int ext2_fs_unmount(void *fs_private) {
    ext2_restore_ctx(fs_private);
    int ret = ext2_umount();
    kfree(fs_private);
    return ret;
}

const struct filesystem_ops ext2_fs_ops = {
    .name     = "ext2",
    .mount    = ext2_fs_mount,
    .open     = ext2_fs_open,
    .stat     = ext2_fs_stat,
    .create   = ext2_fs_create,
    .mkdir    = ext2_fs_mkdir,
    .unlink   = ext2_fs_unlink,
    .read_at  = ext2_fs_read_at,
    .write_at = ext2_fs_write_at,
    .truncate = ext2_fs_truncate,
    .readdir  = ext2_fs_readdir,
    .info     = ext2_fs_info,
    .unmount  = ext2_fs_unmount,
};
