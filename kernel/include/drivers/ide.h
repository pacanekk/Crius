#ifndef IDE_H
#define IDE_H

#include <stddef.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define IDE_MAX_DRIVES 4
#define IDE_MAX_PARTITIONS 4

struct ide_partition {
    int used;
    uint8_t type;
    uint64_t start_lba;
    uint64_t sector_count;
};

struct ide_drive {
    int present;
    uint64_t sector_count;
    char name[8];
    int bus;
    int is_slave;
    struct ide_partition partitions[IDE_MAX_PARTITIONS];
};

void ide_init(void);

int ide_read_sectors(uint64_t lba, size_t count, void *buf);
int ide_write_sectors(uint64_t lba, size_t count, const void *buf);

int ide_read_sectors_dev(int drive_idx, uint64_t lba, size_t count, void *buf);
int ide_write_sectors_dev(int drive_idx, uint64_t lba, size_t count, const void *buf);

int ide_read_partition(int drive_idx, int part_idx, uint64_t lba, size_t count, void *buf);
int ide_write_partition(int drive_idx, int part_idx, uint64_t lba, size_t count, const void *buf);

uint64_t ide_get_sector_count(void);
int ide_present(void);

int ide_drive_count(void);
struct ide_drive *ide_get_drive(int idx);
int ide_find_device(const char *name, int *drive_idx, int *part_idx);

void ide_register_block_devices(void);

#endif
