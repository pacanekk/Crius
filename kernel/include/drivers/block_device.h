#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <stdint.h>
#include <stddef.h>

struct block_device {
    char name[32];
    uint64_t sector_count;
    uint32_t sector_size;
    int (*read)(struct block_device *bd, uint64_t sector, uint32_t count, void *buffer);
    int (*write)(struct block_device *bd, uint64_t sector, uint32_t count, const void *buffer);
    void *private;
};

void block_device_register(struct block_device *bd);
struct block_device *block_device_find(const char *name);
int block_device_count(void);
struct block_device *block_device_get(int idx);

/* Device callbacks for /dev/hda */
int blk_dev_read(char *buf, size_t bufsize);
int blk_dev_write(const char *data, size_t len);
int blk_dev_ioctl(unsigned long request, void *arg);
int blk_dev_present(void);

#endif
