#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <crius/abi.h>
#include "fs/vfs.h"
#include "vfs_internal.h"
#include "fs/ramfs.h"
#include "fs/ext2.h"
#include "drivers/block_device.h"
#include "drivers/serial.h"
#include "mm/kmalloc.h"
#include "process/scheduler.h"
#include "process/task.h"

/* ===== Filesystem registration ===== */

#define MAX_FS_TYPES 8
struct filesystem_ops *registered_fs[MAX_FS_TYPES];

void vfs_register_filesystem(struct filesystem_ops *ops) {
    for (int i = 0; i < MAX_FS_TYPES; i++) {
        if (!registered_fs[i]) {
            registered_fs[i] = ops;
            return;
        }
    }
}

/* ===== Mount table ===== */

struct vfs_mount mounts[VFS_MAX_MOUNTS];

/* ===== Path helpers ===== */

static int path_starts_with(const char *path, const char *prefix) {
    if (prefix[0] == '/' && prefix[1] == '\0') return 1;
    int i = 0;
    while (prefix[i]) {
        if (path[i] != prefix[i]) return 0;
        i++;
    }
    if (path[i] == '\0') return 1;
    if (path[i] == '/') return 1;
    return 0;
}

static int path_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Find all mounts matching path, sorted by prefix length (longest first).
 * For same-length prefixes, lower index first (ramfs root is index 0).
 * Returns count, fills out[] with mount pointers. */
int find_mounts(const char *path, struct vfs_mount **out, int max) {
    int count = 0;
    /* Simple insertion sort by prefix length descending */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) continue;
        int mlen = path_len(mounts[i].path);
        if (!path_starts_with(path, mounts[i].path)) continue;
        /* Insert sorted by mlen descending.
         * For same prefix length: prefer block-device-backed mounts (ext2)
         * over ramfs, so writes/creates go to persistent storage.
         * Among same type, lower index first. */
        int pos = count;
        int new_has_dev = mounts[i].device_bdev != NULL;
        for (int j = 0; j < count; j++) {
            int jlen = path_len(out[j]->path);
            int old_has_dev = out[j]->device_bdev != NULL;
            if (mlen > jlen ||
                (mlen == jlen && new_has_dev && !old_has_dev) ||
                (mlen == jlen && new_has_dev == old_has_dev && i < (int)(out[j] - mounts))) {
                pos = j;
                break;
            }
        }
        if (pos >= max) continue;
        for (int j = count; j > pos; j--) out[j] = out[j - 1];
        out[pos] = &mounts[i];
        count++;
        if (count >= max) break;
    }
    return count;
}

struct vfs_mount *find_mount(const char *path) {
    struct vfs_mount *matches[4];
    int n = find_mounts(path, matches, 4);
    return n > 0 ? matches[0] : NULL;
}

const char *strip_mount(const char *path, struct vfs_mount *m) {
    int mlen = path_len(m->path);
    if (path[mlen] == '\0') return "/";
    if (path[mlen] == '/') return path + mlen;
    return path + mlen - 1;
}

void resolve_abs(const char *path, char *out, int outsize) {
    char tmp[256];

    if (path[0] == '/') {
        int i = 0;
        while (path[i] && i < 255) { tmp[i] = path[i]; i++; }
        tmp[i] = '\0';
    } else {
        const char *cwd = "/";
        struct task *t = task_current();
        if (t && t->cwd[0]) cwd = t->cwd;
        int clen = 0;
        while (cwd[clen] && clen < 127) clen++;

        if (clen == 1 && cwd[0] == '/') {
            tmp[0] = '/';
            int i = 0;
            while (path[i] && i < 254) { tmp[1 + i] = path[i]; i++; }
            tmp[1 + i] = '\0';
        } else {
            int i = 0;
            while (cwd[i] && i < 254) { tmp[i] = cwd[i]; i++; }
            tmp[i++] = '/';
            int j = 0;
            while (path[j] && i < 255) { tmp[i++] = path[j++]; }
            tmp[i] = '\0';
        }
    }

    int pos = 0;
    out[0] = '/';
    pos = 1;

    const char *p = tmp;
    if (*p == '/') p++;

    while (*p) {
        if (*p == '/') { p++; continue; }

        int comp_len = 0;
        const char *comp_start = p;
        while (*p && *p != '/' && comp_len < 255) { p++; comp_len++; }

        if (comp_len == 1 && comp_start[0] == '.') continue;
        if (comp_len == 2 && comp_start[0] == '.' && comp_start[1] == '.') {
            while (pos > 1 && out[pos - 1] != '/') pos--;
            if (pos > 1) pos--;
            continue;
        }

        if (pos > 1 && out[pos - 1] != '/' && pos < outsize - 1) out[pos++] = '/';
        for (int i = 0; i < comp_len && pos < outsize - 1; i++) out[pos++] = comp_start[i];
    }

    if (pos == 0) { out[0] = '/'; pos = 1; }
    out[pos] = '\0';
}

/* ===== vfs_init ===== */

extern void devfs_init(void);
extern void procfs_init(void);

void vfs_init(void) {
    /* Register filesystem types */
    vfs_register_filesystem((struct filesystem_ops *)&ramfs_fs_ops);
    vfs_register_filesystem((struct filesystem_ops *)&ext2_fs_ops);

    /* Mount ramfs as root filesystem at "/" */
    mounts[0].used = 1;
    mounts[0].path[0] = '/'; mounts[0].path[1] = '\0';
    mounts[0].device[0] = '\0';
    mounts[0].ops = (struct filesystem_ops *)&ramfs_fs_ops;
    mounts[0].fs_private = NULL;
    mounts[0].device_bdev = NULL;

    /* Initialize device files (/dev entries in ramfs) */
    devfs_init();

    /* Initialize proc filesystem (/proc entries in ramfs) */
    procfs_init();
}

/* ===== VFS filesystem info query ===== */

int vfs_get_fs_info(const char *mount_path, struct vfs_fs_info *out) {
    struct vfs_mount *matches[4];
    int n = find_mounts(mount_path, matches, 4);
    if (n == 0) return -ENOENT;

    memset(out, 0, sizeof(*out));
    out->mounted = 1;

    /* Prefer the mount that has an info callback (e.g. ext2 over ramfs) */
    struct vfs_mount *m = NULL;
    for (int i = 0; i < n; i++) {
        if (matches[i]->ops && matches[i]->ops->info) {
            m = matches[i];
            break;
        }
    }
    if (!m) m = matches[0];
    if (!m || !m->ops) return -ENOENT;

    /* Copy filesystem name */
    const char *name = m->ops->name;
    int i = 0;
    while (name[i] && i < 15) { out->fs_name[i] = name[i]; i++; }
    out->fs_name[i] = '\0';

    /* Call optional info callback for detailed stats */
    if (m->ops->info) {
        return m->ops->info(m->fs_private, out);
    }

    return 0;
}

/* ===== Directory close - frees dir_handle allocated by vfs_open ===== */

void vfs_dir_close(struct file *f) {
    if (f && f->priv) {
        kfree(f->priv);
        f->priv = NULL;
    }
}

/* ===== vfs_open - try each matching mount in order ===== */

struct file *vfs_open(const char *abs_path, int flags) {
    char abs[256];
    resolve_abs(abs_path, abs, sizeof(abs));

    struct vfs_mount *matches[4];
    int nmounts = find_mounts(abs, matches, 4);
    if (nmounts == 0) return NULL;

    struct file *f = file_alloc();
    if (!f) return NULL;

    f->flags = flags;
    f->offset = 0;
    f->ops = NULL;
    f->priv = NULL;

    struct vfs_mount *m = NULL;
    const char *rel = NULL;

    for (int i = 0; i < nmounts; i++) {
        m = matches[i];
        rel = strip_mount(abs, m);
        if (m->ops->open(m->fs_private, rel, flags, f) == 0) {
            /* Success */
            /* For directories: replace priv with dir_handle for stateful readdir */
            if (f->ops && f->ops->readdir) {
                struct dir_handle *dh = (struct dir_handle *)kmalloc(sizeof(struct dir_handle));
                if (!dh) { file_put(f); return NULL; }
                dh->position = 0;
                dh->mount = m;
                int j = 0;
                while (rel[j] && j < 254) { dh->rel_path[j] = rel[j]; j++; }
                dh->rel_path[j] = '\0';
                f->priv = dh;
            }
            return f;
        }
        /* Reset for next attempt */
        f->ops = NULL;
        f->priv = NULL;
    }

    /* If O_CREAT, try the first mount (ramfs root) */
    if (flags & O_CREAT) {
        m = matches[0];
        rel = strip_mount(abs, m);
        if (m->ops->open(m->fs_private, rel, flags, f) == 0) {
            if (f->ops && f->ops->readdir) {
                struct dir_handle *dh = (struct dir_handle *)kmalloc(sizeof(struct dir_handle));
                if (!dh) { file_put(f); return NULL; }
                dh->position = 0;
                dh->mount = m;
                int j = 0;
                while (rel[j] && j < 254) { dh->rel_path[j] = rel[j]; j++; }
                dh->rel_path[j] = '\0';
                f->priv = dh;
            }
            return f;
        }
    }

    file_put(f);
    return NULL;
}

/* ===== vfs_dir_readdir - stateful, uses dir_handle ===== */

int vfs_dir_readdir(struct file *f, struct dirent *de) {
    if (!f || !f->used || !f->ops || !f->ops->readdir) return -EBADF;
    struct dir_handle *dh = (struct dir_handle *)f->priv;
    if (!dh) return -EBADF;

    int ret = dh->mount->ops->readdir(dh->mount->fs_private, dh->rel_path, dh->position, de);
    if (ret <= 0) return ret;
    dh->position++;
    return 1;
}

/* ===== Path-based VFS operations - try mounts in order ===== */

int vfs_mkdir(const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (matches[i]->ops->mkdir &&
            matches[i]->ops->mkdir(matches[i]->fs_private, strip_mount(abs, matches[i])) == 0)
            return 0;
    }
    return -ENOENT;
}

int vfs_create(const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (matches[i]->ops->create &&
            matches[i]->ops->create(matches[i]->fs_private, strip_mount(abs, matches[i])) == 0)
            return 0;
    }
    return -ENOENT;
}

int vfs_write(const char *path, const char *data, size_t len) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (!matches[i]->ops->write_at) continue;
        int ret = matches[i]->ops->write_at(matches[i]->fs_private, strip_mount(abs, matches[i]), 0, data, len);
        if (ret >= 0) return ret;
    }
    return -ENOENT;
}

int vfs_append(const char *path, const char *data, size_t len) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        struct vfs_mount *m = matches[i];
        const char *rel = strip_mount(abs, m);
        struct stat st;
        if (m->ops->stat && m->ops->stat(m->fs_private, rel, &st) == 0) {
            if (m->ops->write_at)
                return m->ops->write_at(m->fs_private, rel, (uint64_t)st.st_size, data, len);
        }
    }
    /* File not found - create on first mount */
    if (n > 0 && matches[0]->ops->create && matches[0]->ops->write_at) {
        const char *rel = strip_mount(abs, matches[0]);
        if (matches[0]->ops->create(matches[0]->fs_private, rel) == 0)
            return matches[0]->ops->write_at(matches[0]->fs_private, rel, 0, data, len);
    }
    return -ENOENT;
}

int vfs_write_at(const char *path, size_t offset, const char *data, size_t len) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (!matches[i]->ops->write_at) continue;
        int ret = matches[i]->ops->write_at(matches[i]->fs_private, strip_mount(abs, matches[i]), offset, data, len);
        if (ret >= 0) return ret;
    }
    return -ENOENT;
}

int vfs_read(const char *path, char *buf, size_t bufsize) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (!matches[i]->ops->read_at) continue;
        int ret = matches[i]->ops->read_at(matches[i]->fs_private, strip_mount(abs, matches[i]), 0, buf, bufsize);
        if (ret >= 0) return ret;
    }
    return -ENOENT;
}

int vfs_read_at(const char *path, size_t offset, char *buf, size_t count) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (!matches[i]->ops->read_at) continue;
        int ret = matches[i]->ops->read_at(matches[i]->fs_private, strip_mount(abs, matches[i]), offset, buf, count);
        if (ret >= 0) return ret;
    }
    return -ENOENT;
}

int vfs_truncate(const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (matches[i]->ops->truncate &&
            matches[i]->ops->truncate(matches[i]->fs_private, strip_mount(abs, matches[i])) == 0)
            return 0;
    }
    return -ENOENT;
}

int vfs_delete(const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (matches[i]->ops->unlink &&
            matches[i]->ops->unlink(matches[i]->fs_private, strip_mount(abs, matches[i])) == 0)
            return 0;
    }
    return -ENOENT;
}

int vfs_stat(const char *path, struct stat *st) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int n = find_mounts(abs, matches, 4);
    for (int i = 0; i < n; i++) {
        if (matches[i]->ops->stat) {
            memset(st, 0, sizeof(*st));
            int r = matches[i]->ops->stat(matches[i]->fs_private, strip_mount(abs, matches[i]), st);
            if (r == 0) {
                st->st_dev = (uint32_t)(matches[i] - mounts);
                return 0;
            }
        }
    }
    return -ENOENT;
}

/* ===== Directory listing - merge entries from all matching mounts ===== */

static int name_in_buf(const char *buf, int bufsize, const char *name) {
    for (int k = 0; k < bufsize;) {
        int slen = 0;
        while (buf[k + slen] && k + slen < bufsize) slen++;
        if (k + slen >= bufsize) break;
        if (strcmp(buf + k, name) == 0) return 1;
        k += slen + 1;
    }
    return 0;
}

int vfs_list(const char *path, vfs_list_cb cb) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int nmounts = find_mounts(abs, matches, 4);
    if (nmounts == 0) return -ENOENT;

    int count = 0;
    /* Collect names to avoid duplicates */
    char seen[1024];
    int seen_off = 0;

    for (int mi = 0; mi < nmounts; mi++) {
        struct vfs_mount *m = matches[mi];
        if (!m->ops->readdir) continue;
        const char *rel = strip_mount(abs, m);
        uint64_t idx = 0;
        struct dirent de;
        while (m->ops->readdir(m->fs_private, rel, idx, &de) > 0) {
            /* Check for duplicate (skip if seen buffer is full) */
            if (seen_off < 1020 && name_in_buf(seen, seen_off, de.name)) { idx++; continue; }
            /* Add to seen if there's room */
            if (seen_off < 1020) {
                int j;
                for (j = 0; de.name[j] && seen_off < 1023; j++, seen_off++)
                    seen[seen_off] = de.name[j];
                seen[seen_off++] = '\0';
            }

            struct stat st;
            char child_path[512];
            int rlen = 0;
            while (rel[rlen]) rlen++;
            if (rlen == 1 && rel[0] == '/') {
                child_path[0] = '/';
                int i = 0;
                while (de.name[i] && i < 254) { child_path[1 + i] = de.name[i]; i++; }
                child_path[1 + i] = '\0';
            } else {
                int i = 0;
                while (rel[i] && i < 254) { child_path[i] = rel[i]; i++; }
                child_path[i++] = '/';
                int jj = 0;
                while (de.name[jj] && i < 510) { child_path[i++] = de.name[jj++]; }
                child_path[i] = '\0';
            }
            if (m->ops->stat) m->ops->stat(m->fs_private, child_path, &st);
            cb(de.name, de.type, (size_t)st.st_size);
            count++;
            idx++;
        }
    }
    return count;
}

int vfs_list_buf(const char *path, char *buf, int bufsize) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int nmounts = find_mounts(abs, matches, 4);
    if (nmounts == 0) return -ENOENT;

    int off = 0;
    int count = 0;

    for (int mi = 0; mi < nmounts; mi++) {
        struct vfs_mount *m = matches[mi];
        if (!m->ops->readdir) continue;
        const char *rel = strip_mount(abs, m);
        uint64_t idx = 0;
        struct dirent de;
        while (m->ops->readdir(m->fs_private, rel, idx, &de) > 0) {
            if (off >= bufsize - 1) break;
            /* Check for duplicate */
            if (name_in_buf(buf, off, de.name)) { idx++; continue; }
            int j;
            for (j = 0; de.name[j] && off < bufsize - 1; j++, off++)
                buf[off] = de.name[j];
            buf[off++] = '\0';
            count++;
            idx++;
        }
    }
    if (off < bufsize) buf[off] = '\0';
    return count;
}

/* ===== vfs_readdir (path-based) - merge entries from all mounts ===== */

int vfs_readdir(const char *abs_path, int index, struct dirent *out) {
    char abs[256];
    resolve_abs(abs_path, abs, sizeof(abs));
    struct vfs_mount *matches[4];
    int nmounts = find_mounts(abs, matches, 4);
    if (nmounts == 0) return -ENOENT;

    /* Iterate through all mounts, merging entries, skip duplicates */
    char seen[1024];
    int seen_off = 0;
    int current = 0;

    for (int mi = 0; mi < nmounts; mi++) {
        struct vfs_mount *m = matches[mi];
        if (!m->ops->readdir) continue;
        const char *rel = strip_mount(abs, m);
        uint64_t idx = 0;
        struct dirent de;
        while (m->ops->readdir(m->fs_private, rel, idx, &de) > 0) {
            if (seen_off >= 1020 || !name_in_buf(seen, seen_off, de.name)) {
                /* Add to seen if there's room */
                if (seen_off < 1020) {
                    int j;
                    for (j = 0; de.name[j] && seen_off < 1023; j++, seen_off++)
                        seen[seen_off] = de.name[j];
                    seen[seen_off++] = '\0';
                }

                if (current == index) {
                    out->inode = de.inode;
                    out->type = de.type;
                    int k = 0;
                    while (de.name[k] && k < 255) { out->name[k] = de.name[k]; k++; }
                    out->name[k] = '\0';
                    return 1;
                }
                current++;
            }
            idx++;
        }
    }
    return 0;
}

/* ===== chdir / pwd ===== */

int vfs_chdir(const char *path) {
    char abs[256];
    resolve_abs(path, abs, sizeof(abs));

    struct vfs_mount *m = find_mount(abs);
    if (!m) return -ENOENT;
    struct stat st;
    if (m->ops->stat(m->fs_private, strip_mount(abs, m), &st) < 0) return -ENOENT;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;

    struct task *t = task_current();
    if (!t) return -EINVAL;
    int i = 0;
    while (abs[i] && i < 127) { t->cwd[i] = abs[i]; i++; }
    t->cwd[i] = '\0';
    return 0;
}

int vfs_pwd(char *buf, int bufsize) {
    struct task *t = task_current();
    const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
    int i = 0;
    while (cwd[i] && i < bufsize - 1) { buf[i] = cwd[i]; i++; }
    buf[i] = '\0';
    return 0;
}

void vfs_resolve_abs(const char *path, char *out, int outsize) {
    resolve_abs(path, out, outsize);
}
