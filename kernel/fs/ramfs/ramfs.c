#include <stdint.h>
#include <string.h>
#include "fs/ramfs.h"
#include "ramfs_internal.h"
#include "fs/vfs.h"

static struct ramfs_inode inodes[MAX_INODES];
static int cwd_inode;

static int alloc_inode(void) {
    for (int i = 0; i < MAX_INODES; i++) {
        if (!inodes[i].used)
            return i;
    }
    return -1;
}

static int dir_find(int dir, const char *name) {
    if (inodes[dir].type != T_DIR) return -1;
    for (int i = 0; i < inodes[dir].entry_count; i++) {
        int idx = inodes[dir].entries[i];
        if (idx < 0 || !inodes[idx].used) continue;
        if (strcmp(inodes[idx].name, name) == 0)
            return idx;
    }
    return -1;
}

static int dir_add(int dir, int idx) {
    if (inodes[dir].entry_count >= MAX_DIR_ENT) return -1;
    for (int i = 0; i < inodes[dir].entry_count; i++) {
        if (inodes[dir].entries[i] == idx) return 0;
    }
    inodes[dir].entries[inodes[dir].entry_count++] = idx;
    return 0;
}

static int dir_remove(int dir, int idx) {
    for (int i = 0; i < inodes[dir].entry_count; i++) {
        if (inodes[dir].entries[i] == idx) {
            inodes[dir].entries[i] = inodes[dir].entries[--inodes[dir].entry_count];
            return 0;
        }
    }
    return -1;
}

static int resolve_path(const char *path, int base) {
    if (path[0] == '/') base = 0;
    int cur = base;
    int i = 0;
    while (path[i]) {
        while (path[i] == '/') i++;
        if (!path[i]) break;
        int start = i;
        while (path[i] && path[i] != '/') i++;
        int len = i - start;
        if (len == 1 && path[start] == '.') continue;
        if (len == 2 && path[start] == '.' && path[start+1] == '.') {
            if (cur != 0) cur = inodes[cur].parent;
            continue;
        }
        char name[MAX_NAME];
        if (len >= MAX_NAME) len = MAX_NAME - 1;
        for (int j = 0; j < len; j++) name[j] = path[start + j];
        name[len] = '\0';
        cur = dir_find(cur, name);
        if (cur < 0) return -1;
    }
    return cur;
}

void ramfs_init(void) {
    for (int i = 0; i < MAX_INODES; i++) {
        inodes[i].used = 0;
        inodes[i].entry_count = 0;
    }
    inodes[0].used = 1;
    inodes[0].type = T_DIR;
    inodes[0].name[0] = '/';
    inodes[0].name[1] = '\0';
    inodes[0].parent = 0;
    inodes[0].size = 0;
    inodes[0].entry_count = 0;
    cwd_inode = 0;
}

int ramfs_resolve(const char *path, int *out_inode) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    if (out_inode) *out_inode = idx;
    return 0;
}

int ramfs_mkdir(const char *path) {
    int parent;
    const char *name;
    int slash = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') slash = i;
    if (slash >= 0) {
        char parent_path[MAX_NAME * 4];
        int plen = slash;
        if (plen == 0) { parent_path[0] = '/'; parent_path[1] = '\0'; }
        else { for (int i = 0; i < plen; i++) parent_path[i] = path[i]; parent_path[plen] = '\0'; }
        parent = resolve_path(parent_path, cwd_inode);
        if (parent < 0) return -1;
        if (inodes[parent].type != T_DIR) return -1;
        name = path + slash + 1;
    } else {
        parent = cwd_inode;
        name = path;
    }
    if (!name[0]) return -1;
    if (dir_find(parent, name) >= 0) return -1;
    int idx = alloc_inode();
    if (idx < 0) return -1;
    inodes[idx].used = 1;
    inodes[idx].type = T_DIR;
    inodes[idx].parent = parent;
    inodes[idx].size = 0;
    inodes[idx].entry_count = 0;
    int j;
    for (j = 0; name[j] && j < MAX_NAME - 1; j++) inodes[idx].name[j] = name[j];
    inodes[idx].name[j] = '\0';
    if (dir_add(parent, idx) < 0) { inodes[idx].used = 0; return -1; }
    return 0;
}

static int create_entry(const char *path, int type, dev_read_fn rfn, dev_write_fn wfn, dev_ioctl_fn ifn) {
    int parent;
    const char *name;
    int slash = -1;
    for (int i = 0; path[i]; i++) if (path[i] == '/') slash = i;
    if (slash >= 0) {
        char parent_path[MAX_NAME * 4];
        int plen = slash;
        if (plen == 0) { parent_path[0] = '/'; parent_path[1] = '\0'; }
        else { for (int i = 0; i < plen; i++) parent_path[i] = path[i]; parent_path[plen] = '\0'; }
        parent = resolve_path(parent_path, cwd_inode);
        if (parent < 0) return -1;
        if (inodes[parent].type != T_DIR) return -1;
        name = path + slash + 1;
    } else {
        parent = cwd_inode;
        name = path;
    }
    if (!name[0]) return -1;
    if (dir_find(parent, name) >= 0) return -1;
    int idx = alloc_inode();
    if (idx < 0) return -1;
    inodes[idx].used = 1;
    inodes[idx].type = type;
    inodes[idx].parent = parent;
    inodes[idx].size = 0;
    inodes[idx].entry_count = 0;
    inodes[idx].dev_read = rfn;
    inodes[idx].dev_write = wfn;
    inodes[idx].dev_ioctl = ifn;
    inodes[idx].stream = 0;
    int j;
    for (j = 0; name[j] && j < MAX_NAME - 1; j++) inodes[idx].name[j] = name[j];
    inodes[idx].name[j] = '\0';
    if (dir_add(parent, idx) < 0) { inodes[idx].used = 0; return -1; }
    return 0;
}

int ramfs_create(const char *path) {
    return create_entry(path, T_FILE, NULL, NULL, NULL);
}

int ramfs_create_dev(const char *path, dev_read_fn rfn, dev_write_fn wfn) {
    return create_entry(path, T_DEV, rfn, wfn, NULL);
}

int ramfs_create_dev_ioctl(const char *path, dev_read_fn rfn, dev_write_fn wfn, dev_ioctl_fn ifn) {
    return create_entry(path, T_DEV, rfn, wfn, ifn);
}

int ramfs_create_dev_stream(const char *path, dev_read_fn rfn, dev_write_fn wfn) {
    int ret = create_entry(path, T_DEV, rfn, wfn, NULL);
    if (ret < 0) return ret;
    int found;
    if (ramfs_resolve(path, &found) == 0) {
        inodes[found].stream = 1;
    }
    return 0;
}

int ramfs_write(const char *path, const char *data, size_t len) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) {
        if (ramfs_create(path) < 0) return -1;
        idx = resolve_path(path, cwd_inode);
        if (idx < 0) return -1;
    }
    if (inodes[idx].type == T_DEV) {
        if (inodes[idx].dev_write) return inodes[idx].dev_write(data, len);
        return -1;
    }
    if (inodes[idx].type != T_FILE) return -1;
    if (len > MAX_FILESIZE) len = MAX_FILESIZE;
    for (size_t i = 0; i < len; i++) inodes[idx].data[i] = data[i];
    inodes[idx].size = len;
    return (int)len;
}

int ramfs_write_at(int idx, size_t offset, const char *data, size_t len) {
    if (idx < 0 || idx >= MAX_INODES) return -1;
    if (inodes[idx].type != T_FILE) return -1;
    if (offset + len > MAX_FILESIZE) len = MAX_FILESIZE - offset;
    for (size_t i = 0; i < len; i++)
        inodes[idx].data[offset + i] = data[i];
    if (offset + len > inodes[idx].size)
        inodes[idx].size = offset + len;
    return (int)len;
}

int ramfs_append(const char *path, const char *data, size_t len) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) {
        if (ramfs_create(path) < 0) return -1;
        idx = resolve_path(path, cwd_inode);
        if (idx < 0) return -1;
    }
    if (inodes[idx].type == T_DEV) {
        if (inodes[idx].dev_write) return inodes[idx].dev_write(data, len);
        return -1;
    }
    if (inodes[idx].type != T_FILE) return -1;
    if (inodes[idx].size > 0 && inodes[idx].data[inodes[idx].size - 1] != '\n') {
        if (inodes[idx].size < MAX_FILESIZE)
            inodes[idx].data[inodes[idx].size++] = '\n';
    }
    if (inodes[idx].size + len > MAX_FILESIZE)
        len = MAX_FILESIZE - inodes[idx].size;
    for (size_t i = 0; i < len; i++)
        inodes[idx].data[inodes[idx].size + i] = data[i];
    inodes[idx].size += len;
    return (int)len;
}

int ramfs_read_at(int idx, size_t offset, char *buf, size_t count) {
    if (idx < 0 || idx >= MAX_INODES) return -1;
    if (inodes[idx].type != T_FILE) return -1;
    if (offset >= inodes[idx].size) return 0;
    size_t avail = inodes[idx].size - offset;
    if (count > avail) count = avail;
    for (size_t i = 0; i < count; i++)
        buf[i] = inodes[idx].data[offset + i];
    return (int)count;
}

int ramfs_read(const char *path, char *buf, size_t bufsize) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    if (inodes[idx].type == T_DEV) {
        if (inodes[idx].dev_read) return inodes[idx].dev_read(buf, bufsize);
        return -1;
    }
    if (inodes[idx].type != T_FILE) return -1;
    size_t to_copy = inodes[idx].size < bufsize ? inodes[idx].size : bufsize;
    for (size_t i = 0; i < to_copy; i++) buf[i] = inodes[idx].data[i];
    return (int)to_copy;
}

int ramfs_delete(const char *path) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    if (idx == 0) return -1;
    if (inodes[idx].type == T_DIR && inodes[idx].entry_count > 0) return -1;
    dir_remove(inodes[idx].parent, idx);
    inodes[idx].used = 0;
    inodes[idx].size = 0;
    inodes[idx].entry_count = 0;
    inodes[idx].dev_read = NULL;
    inodes[idx].dev_write = NULL;
    inodes[idx].name[0] = '\0';
    return 0;
}

int ramfs_list_dir(const char *path, int *out_indices, int max) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    if (inodes[idx].type != T_DIR) return -1;
    int count = inodes[idx].entry_count;
    if (count > max) count = max;
    for (int i = 0; i < count; i++)
        out_indices[i] = inodes[idx].entries[i];
    return count;
}

int ramfs_stat(const char *path, int *type, size_t *size) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    if (type) *type = inodes[idx].type;
    if (size) *size = inodes[idx].size;
    return 0;
}

int ramfs_chdir(const char *path) {
    int idx = resolve_path(path, cwd_inode);
    if (idx < 0) return -1;
    if (inodes[idx].type != T_DIR) return -1;
    cwd_inode = idx;
    return 0;
}

void ramfs_pwd(char *buf, int bufsize) {
    if (cwd_inode == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        return;
    }
    int parts[MAX_INODES];
    int n = 0;
    int cur = cwd_inode;
    while (cur != 0 && n < MAX_INODES) {
        parts[n++] = cur;
        cur = inodes[cur].parent;
    }
    int pos = 0;
    for (int i = n - 1; i >= 0 && pos < bufsize - 1; i--) {
        buf[pos++] = '/';
        for (int j = 0; inodes[parts[i]].name[j] && pos < bufsize - 1; j++)
            buf[pos++] = inodes[parts[i]].name[j];
    }
    if (pos == 0) buf[pos++] = '/';
    buf[pos] = '\0';
}

struct ramfs_inode *ramfs_get_inode(int idx) {
    if (idx < 0 || idx >= MAX_INODES) return NULL;
    if (!inodes[idx].used) return NULL;
    return &inodes[idx];
}
