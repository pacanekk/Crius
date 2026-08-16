#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "drivers/ide.h"
#include "arch/io.h"
#include "drivers/serial.h"
#include "mm/kmalloc.h"
#include "drivers/block_device.h"

#define ATA_PRIMARY_DATA     0x1F0
#define ATA_PRIMARY_ERROR    0x1F1
#define ATA_PRIMARY_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LO   0x1F3
#define ATA_PRIMARY_LBA_MID  0x1F4
#define ATA_PRIMARY_LBA_HI   0x1F5
#define ATA_PRIMARY_DRIVE    0x1F6
#define ATA_PRIMARY_COMMAND  0x1F7
#define ATA_PRIMARY_CONTROL  0x3F6

#define ATA_SECONDARY_DATA     0x170
#define ATA_SECONDARY_ERROR    0x171
#define ATA_SECONDARY_COUNT    0x172
#define ATA_SECONDARY_LBA_LO   0x173
#define ATA_SECONDARY_LBA_MID  0x174
#define ATA_SECONDARY_LBA_HI   0x175
#define ATA_SECONDARY_DRIVE    0x176
#define ATA_SECONDARY_COMMAND  0x177
#define ATA_SECONDARY_CONTROL  0x376

#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_IDENTIFY     0xEC

#define ATA_SR_ERR           0x01
#define ATA_SR_DRQ           0x08
#define ATA_SR_DF            0x20

static struct ide_drive drives[IDE_MAX_DRIVES];
static int drive_count = 0;

static int get_base(int bus) {
    return bus == 0 ? ATA_PRIMARY_DATA : ATA_SECONDARY_DATA;
}

static int get_control(int bus) {
    return bus == 0 ? ATA_PRIMARY_CONTROL : ATA_SECONDARY_CONTROL;
}

static int get_cmd(int bus) {
    return bus == 0 ? ATA_PRIMARY_COMMAND : ATA_SECONDARY_COMMAND;
}

static void ata_wait(int bus) {
    for (int i = 0; i < 4; i++)
        inb(get_control(bus));
}

static int ata_wait_drq(int bus) {
    uint8_t status;
    int timeout = 100000;
    do {
        status = inb(get_cmd(bus));
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DF) return -1;
        if (timeout-- <= 0) return -1;
    } while (!(status & ATA_SR_DRQ));
    return 0;
}

static int ata_wait_busy(int bus) {
    uint8_t status;
    int timeout = 100000;
    do {
        status = inb(get_cmd(bus));
        if (timeout-- <= 0) return -1;
    } while (status & 0x80);
    return 0;
}

static int probe_drive(int bus, int slave) {
    int base = get_base(bus);
    int ctrl = get_control(bus);
    int cmd = get_cmd(bus);

    outb(ctrl, 0x00);
    ata_wait(bus);

    outb(base + 6, slave ? 0xF0 : 0xE0);
    ata_wait(bus);
    outb(cmd, ATA_CMD_IDENTIFY);
    ata_wait(bus);

    uint8_t status = inb(cmd);
    if (status == 0) return 0;

    if (ata_wait_busy(bus) < 0) return 0;

    status = inb(cmd);
    if (status & ATA_SR_ERR) return 0;

    if (ata_wait_drq(bus) < 0) return 0;

    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(base);
    }

    uint64_t sectors = 0;
    uint32_t lba28 = identify[60] | ((uint32_t)identify[61] << 16);
    if (lba28 > 0) {
        sectors = lba28;
    } else {
        uint32_t lo = identify[100];
        uint32_t hi = identify[101];
        sectors = ((uint64_t)hi << 32) | lo;
    }

    if (sectors == 0) return 0;

    int idx = drive_count;
    if (idx >= IDE_MAX_DRIVES) return 0;

    drives[idx].present = 1;
    drives[idx].sector_count = sectors;
    drives[idx].bus = bus;
    drives[idx].is_slave = slave;

    char *name = drives[idx].name;
    name[0] = 'h'; name[1] = 'd'; name[2] = 'a' + idx; name[3] = '\0';

    serial_puts("IDE: ");
    serial_puts(name);
    serial_puts(" present, sectors=");
    char buf[20]; int pos = 0;
    uint64_t sc = sectors;
    if (sc == 0) { buf[pos++] = '0'; }
    else { char tmp[20]; int tm = 0; while (sc) { tmp[tm++] = '0' + (sc % 10); sc /= 10; } while (tm > 0) buf[pos++] = tmp[--tm]; }
    buf[pos] = '\0';
    serial_puts(buf);
    serial_puts("\n");

    drive_count++;
    return 1;
}

static void parse_partitions(int drive_idx) {
    struct ide_drive *drv = &drives[drive_idx];
    uint8_t *mbr = kmalloc(512);
    if (!mbr) return;

    if (ide_read_sectors_dev(drive_idx, 0, 1, mbr) < 0) {
        kfree(mbr);
        return;
    }

    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        kfree(mbr);
        return;
    }

    for (int i = 0; i < 4; i++) {
        uint8_t *entry = mbr + 446 + i * 16;
        uint8_t ptype = entry[4];
        if (ptype == 0) continue;

        uint32_t start = entry[8] | ((uint32_t)entry[9] << 8) |
                         ((uint32_t)entry[10] << 16) | ((uint32_t)entry[11] << 24);
        uint32_t size = entry[12] | ((uint32_t)entry[13] << 8) |
                        ((uint32_t)entry[14] << 16) | ((uint32_t)entry[15] << 24);

        if (start == 0 || size == 0) continue;

        drv->partitions[i].used = 1;
        drv->partitions[i].type = ptype;
        drv->partitions[i].start_lba = start;
        drv->partitions[i].sector_count = size;

        serial_puts("  ");
        serial_puts(drv->name);
        char digit[2] = {'0' + i + 1, 0};
        serial_puts(digit);
        serial_puts(": type=0x");
        char hb[4]; int hn = 0;
        uint8_t t = ptype;
        if (t == 0) hb[hn++] = '0'; else { char tmp[4]; int tm = 0; while (t) { int d = t & 0xF; tmp[tm++] = d < 10 ? '0'+d : 'A'+d-10; t >>= 4; } while (tm > 0) hb[hn++] = tmp[--tm]; }
        hb[hn] = 0;
        serial_puts(hb);
        serial_puts(" start=");
        char sb[16]; int sn = 0;
        uint32_t s = start;
        if (s == 0) sb[sn++] = '0'; else { char tmp[16]; int tm = 0; while (s) { tmp[tm++] = '0' + (s % 10); s /= 10; } while (tm > 0) sb[sn++] = tmp[--tm]; }
        sb[sn] = 0;
        serial_puts(sb);
        serial_puts("\n");
    }

    kfree(mbr);
}

void ide_init(void) {
    drive_count = 0;
    for (int i = 0; i < IDE_MAX_DRIVES; i++) {
        drives[i].present = 0;
        for (int j = 0; j < IDE_MAX_PARTITIONS; j++)
            drives[i].partitions[j].used = 0;
    }

    probe_drive(0, 0);
    probe_drive(0, 1);
    probe_drive(1, 0);
    probe_drive(1, 1);

    for (int i = 0; i < drive_count; i++) {
        parse_partitions(i);
    }
}

int ide_present(void) {
    return drive_count > 0;
}

uint64_t ide_get_sector_count(void) {
    if (drive_count > 0) return drives[0].sector_count;
    return 0;
}

int ide_drive_count(void) {
    return drive_count;
}

struct ide_drive *ide_get_drive(int idx) {
    if (idx < 0 || idx >= drive_count) return NULL;
    return &drives[idx];
}

int ide_read_sectors_dev(int drive_idx, uint64_t lba, size_t count, void *buf) {
    if (drive_idx < 0 || drive_idx >= drive_count || count == 0) return -1;
    struct ide_drive *drv = &drives[drive_idx];
    if (!drv->present) return -1;

    int base = get_base(drv->bus);
    int cmd = get_cmd(drv->bus);

    uint16_t *p = (uint16_t *)buf;
    size_t done = 0;
    while (done < count) {
        size_t batch = count - done;
        if (batch > 256) batch = 256;

        outb(base + 6, (drv->is_slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
        ata_wait(drv->bus);
        outb(base + 2, (uint8_t)(batch & 0xFF));
        outb(base + 3, (uint8_t)(lba & 0xFF));
        outb(base + 4, (uint8_t)((lba >> 8) & 0xFF));
        outb(base + 5, (uint8_t)((lba >> 16) & 0xFF));
        outb(cmd, ATA_CMD_READ_PIO);

        for (size_t s = 0; s < batch; s++) {
            if (ata_wait_busy(drv->bus) < 0) return -1;
            if (ata_wait_drq(drv->bus) < 0) return -1;
            for (int i = 0; i < 256; i++) {
                p[(done + s) * 256 + i] = inw(base);
            }
        }

        lba += batch;
        done += batch;
    }

    ata_wait_busy(drv->bus);
    return 0;
}

int ide_write_sectors_dev(int drive_idx, uint64_t lba, size_t count, const void *buf) {
    if (drive_idx < 0 || drive_idx >= drive_count || count == 0) return -1;
    struct ide_drive *drv = &drives[drive_idx];
    if (!drv->present) return -1;

    int base = get_base(drv->bus);
    int cmd = get_cmd(drv->bus);

    const uint16_t *p = (const uint16_t *)buf;
    size_t done = 0;
    while (done < count) {
        size_t batch = count - done;
        if (batch > 256) batch = 256;

        outb(base + 6, (drv->is_slave ? 0xF0 : 0xE0) | ((lba >> 24) & 0x0F));
        ata_wait(drv->bus);
        outb(base + 2, (uint8_t)(batch & 0xFF));
        outb(base + 3, (uint8_t)(lba & 0xFF));
        outb(base + 4, (uint8_t)((lba >> 8) & 0xFF));
        outb(base + 5, (uint8_t)((lba >> 16) & 0xFF));
        outb(cmd, ATA_CMD_WRITE_PIO);

        for (size_t s = 0; s < batch; s++) {
            if (ata_wait_busy(drv->bus) < 0) return -1;
            if (ata_wait_drq(drv->bus) < 0) return -1;
            for (int i = 0; i < 256; i++) {
                outw(base, p[(done + s) * 256 + i]);
            }
        }

        lba += batch;
        done += batch;
    }

    outb(cmd, 0xE6);
    ata_wait_busy(drv->bus);
    return 0;
}

int ide_read_partition(int drive_idx, int part_idx, uint64_t lba, size_t count, void *buf) {
    if (drive_idx < 0 || drive_idx >= drive_count) return -1;
    if (part_idx < 0 || part_idx >= IDE_MAX_PARTITIONS) return -1;
    struct ide_drive *drv = &drives[drive_idx];
    if (!drv->partitions[part_idx].used) return -1;
    return ide_read_sectors_dev(drive_idx, drv->partitions[part_idx].start_lba + lba, count, buf);
}

int ide_write_partition(int drive_idx, int part_idx, uint64_t lba, size_t count, const void *buf) {
    if (drive_idx < 0 || drive_idx >= drive_count) return -1;
    if (part_idx < 0 || part_idx >= IDE_MAX_PARTITIONS) return -1;
    struct ide_drive *drv = &drives[drive_idx];
    if (!drv->partitions[part_idx].used) return -1;
    return ide_write_sectors_dev(drive_idx, drv->partitions[part_idx].start_lba + lba, count, buf);
}

int ide_find_device(const char *name, int *drive_idx, int *part_idx) {
    for (int d = 0; d < drive_count; d++) {
        int name_len = 0;
        while (drives[d].name[name_len]) name_len++;

        int match = 1;
        for (int i = 0; i < name_len; i++) {
            if (name[i] != drives[d].name[i]) { match = 0; break; }
        }
        if (!match) continue;

        if (name[name_len] == '\0') {
            *drive_idx = d;
            *part_idx = -1;
            return 0;
        }

        if (name[name_len] >= '1' && name[name_len] <= '4') {
            int p = name[name_len] - '1';
            if (drives[d].partitions[p].used) {
                *drive_idx = d;
                *part_idx = p;
                return 0;
            }
        }
    }
    return -1;
}

int ide_read_sectors(uint64_t lba, size_t count, void *buf) {
    return ide_read_sectors_dev(0, lba, count, buf);
}

int ide_write_sectors(uint64_t lba, size_t count, const void *buf) {
    return ide_write_sectors_dev(0, lba, count, buf);
}

/* ===== Block device wrappers for IDE ===== */

struct ide_bd_priv {
    int drive_idx;
    int part_idx;  /* -1 for whole disk */
};

static int ide_bd_read(struct block_device *bd, uint64_t sector, uint32_t count, void *buffer) {
    struct ide_bd_priv *p = (struct ide_bd_priv *)bd->private;
    if (p->part_idx >= 0)
        return ide_read_partition(p->drive_idx, p->part_idx, sector, count, buffer);
    return ide_read_sectors_dev(p->drive_idx, sector, count, buffer);
}

static int ide_bd_write(struct block_device *bd, uint64_t sector, uint32_t count, const void *buffer) {
    struct ide_bd_priv *p = (struct ide_bd_priv *)bd->private;
    if (p->part_idx >= 0)
        return ide_write_partition(p->drive_idx, p->part_idx, sector, count, buffer);
    return ide_write_sectors_dev(p->drive_idx, sector, count, buffer);
}

static struct block_device ide_block_devices[IDE_MAX_DRIVES * (1 + IDE_MAX_PARTITIONS)];
static struct ide_bd_priv ide_bd_privs[IDE_MAX_DRIVES * (1 + IDE_MAX_PARTITIONS)];

void ide_register_block_devices(void) {
    int bd_idx = 0;
    for (int d = 0; d < drive_count; d++) {
        struct ide_drive *drv = &drives[d];
        if (!drv->present) continue;

        /* Whole disk device */
        if (bd_idx < (int)(sizeof(ide_block_devices)/sizeof(ide_block_devices[0]))) {
            struct block_device *bd = &ide_block_devices[bd_idx];
            struct ide_bd_priv *priv = &ide_bd_privs[bd_idx];
            priv->drive_idx = d;
            priv->part_idx = -1;
            memset(bd->name, 0, sizeof(bd->name));
            for (int i = 0; drv->name[i] && i < 31; i++) bd->name[i] = drv->name[i];
            bd->sector_count = drv->sector_count;
            bd->sector_size = SECTOR_SIZE;
            bd->read = ide_bd_read;
            bd->write = ide_bd_write;
            bd->private = priv;
            block_device_register(bd);
            bd_idx++;
        }

        /* Partition devices */
        for (int p = 0; p < IDE_MAX_PARTITIONS; p++) {
            if (!drv->partitions[p].used) continue;
            if (bd_idx >= (int)(sizeof(ide_block_devices)/sizeof(ide_block_devices[0]))) break;
            struct block_device *bd = &ide_block_devices[bd_idx];
            struct ide_bd_priv *priv = &ide_bd_privs[bd_idx];
            priv->drive_idx = d;
            priv->part_idx = p;
            memset(bd->name, 0, sizeof(bd->name));
            for (int i = 0; drv->name[i] && i < 31; i++) bd->name[i] = drv->name[i];
            char digit[2] = {'0' + p + 1, 0};
            int nlen = 0;
            while (bd->name[nlen]) nlen++;
            bd->name[nlen] = digit[0];
            bd->name[nlen + 1] = '\0';
            bd->sector_count = drv->partitions[p].sector_count;
            bd->sector_size = SECTOR_SIZE;
            bd->read = ide_bd_read;
            bd->write = ide_bd_write;
            bd->private = priv;
            block_device_register(bd);
            bd_idx++;
        }
    }
}
