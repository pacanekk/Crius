#include <string.h>
#include <stdint.h>
#include <crius/abi.h>
#include "drivers/block_device.h"
#include "fs/vfs.h"

#define MAX_BLOCK_DEVICES 8

static struct block_device *registered[MAX_BLOCK_DEVICES];

void block_device_register(struct block_device *bd) {
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (!registered[i]) {
            registered[i] = bd;
            return;
        }
    }
}

struct block_device *block_device_find(const char *name) {
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (registered[i] && strcmp(registered[i]->name, name) == 0)
            return registered[i];
    }
    return NULL;
}

int block_device_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++)
        if (registered[i]) n++;
    return n;
}

struct block_device *block_device_get(int idx) {
    int n = 0;
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        if (registered[i]) {
            if (n == idx) return registered[i];
            n++;
        }
    }
    return NULL;
}

/* ===== Device callbacks for /dev/hda ===== */

static void blk_put_str(char *buf, int *pos, int bufsize, const char *s) {
    while (*s && *pos < bufsize - 1) buf[(*pos)++] = *s++;
}

static void blk_put_uint(char *buf, int *pos, int bufsize, uint64_t val) {
    if (val == 0) { if (*pos < bufsize - 1) buf[(*pos)++] = '0'; return; }
    char tmp[20]; int tm = 0;
    while (val) { tmp[tm++] = '0' + (val % 10); val /= 10; }
    while (tm > 0 && *pos < bufsize - 1) buf[(*pos)++] = tmp[--tm];
}

int blk_dev_present(void) {
    return block_device_find("hda") != NULL;
}

int blk_dev_read(char *buf, size_t bufsize) {
    int pos = 0;
    struct block_device *bd = block_device_find("hda");
    if (!bd) {
        blk_put_str(buf, &pos, bufsize, "No block device\n");
        buf[pos] = '\0';
        return pos;
    }
    blk_put_str(buf, &pos, bufsize, "device: ");
    blk_put_str(buf, &pos, bufsize, bd->name);
    blk_put_str(buf, &pos, bufsize, "\nsectors: ");
    blk_put_uint(buf, &pos, bufsize, bd->sector_count);
    blk_put_str(buf, &pos, bufsize, "\nsize_mb: ");
    blk_put_uint(buf, &pos, bufsize, bd->sector_count / 2048);
    blk_put_str(buf, &pos, bufsize, "\n");

    struct vfs_fs_info info;
    if (vfs_get_fs_info("/", &info) == 0 && info.mounted) {
        blk_put_str(buf, &pos, bufsize, "fs: ");
        blk_put_str(buf, &pos, bufsize, info.fs_name);
        blk_put_str(buf, &pos, bufsize, "\n");
        if (info.block_size > 0) {
            blk_put_str(buf, &pos, bufsize, "block_size: ");
            blk_put_uint(buf, &pos, bufsize, info.block_size);
            blk_put_str(buf, &pos, bufsize, "\ntotal_blocks: ");
            blk_put_uint(buf, &pos, bufsize, info.total_blocks);
            blk_put_str(buf, &pos, bufsize, "\nfree_blocks: ");
            blk_put_uint(buf, &pos, bufsize, info.free_blocks);
            blk_put_str(buf, &pos, bufsize, "\ntotal_inodes: ");
            blk_put_uint(buf, &pos, bufsize, info.total_inodes);
            blk_put_str(buf, &pos, bufsize, "\nfree_inodes: ");
            blk_put_uint(buf, &pos, bufsize, info.free_inodes);
            blk_put_str(buf, &pos, bufsize, "\n");
        }
    } else {
        blk_put_str(buf, &pos, bufsize, "fs: none\n");
    }
    buf[pos] = '\0';
    return pos;
}

int blk_dev_write(const char *data, size_t len) {
    struct block_device *bd = block_device_find("hda");
    if (!bd) return -1;
    char sector[512];
    for (int i = 0; i < 512; i++) sector[i] = 0;
    size_t copy = len < 512 ? len : 512;
    for (size_t i = 0; i < copy; i++) sector[i] = data[i];
    if (bd->write(bd, 0, 1, sector) < 0) return -1;
    return (int)len;
}

int blk_dev_ioctl(unsigned long request, void *arg) {
    struct block_device *bd = block_device_find("hda");
    if (!bd) return -1;
    switch (request) {
    case BLK_GET_INFO: {
        struct block_dev_info *out = (struct block_dev_info *)arg;
        out->present = 1;
        out->sector_count = bd->sector_count;
        memset(out->name, 0, 8);
        for (int i = 0; bd->name[i] && i < 7; i++) out->name[i] = bd->name[i];
        for (int p = 0; p < MAX_PARTITIONS; p++) {
            out->partitions[p].used = 0;
            out->partitions[p].type = 0;
            out->partitions[p].start_lba = 0;
            out->partitions[p].sector_count = 0;
        }
        for (int p = 0; p < MAX_PARTITIONS; p++) {
            char pname[32];
            int nlen = 0;
            while (bd->name[nlen] && nlen < 30) { pname[nlen] = bd->name[nlen]; nlen++; }
            pname[nlen] = '0' + p + 1;
            pname[nlen + 1] = '\0';
            struct block_device *pbd = block_device_find(pname);
            if (pbd) {
                out->partitions[p].used = 1;
                out->partitions[p].type = 0;
                out->partitions[p].start_lba = 0;
                out->partitions[p].sector_count = pbd->sector_count;
            }
        }
        return 0;
    }
    case BLK_READ_SECTOR: {
        struct blk_io *io = (struct blk_io *)arg;
        return bd->read(bd, io->lba, io->count, io->buf);
    }
    case BLK_WRITE_SECTOR: {
        struct blk_io *io = (struct blk_io *)arg;
        return bd->write(bd, io->lba, io->count, io->buf);
    }
    default:
        return -1;
    }
}
