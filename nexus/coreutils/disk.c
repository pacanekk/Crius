#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <mount.h>
#include <crius/abi.h>
#include "api.h"

static int hda_ioctl(unsigned long request, void *arg) {
    int fd = open("/dev/hda", O_RDONLY);
    if (fd < 0) return -1;
    int ret = ioctl(fd, request, arg);
    close(fd);
    return ret;
}

int disk_main(int argc, char **argv) {
    (void)argc; (void)argv;
    struct block_dev_info dev;
    if (hda_ioctl(BLK_GET_INFO, &dev) < 0 || !dev.present) {
        prog_print_color("No IDE drive detected\n", 0x00FF0000, 0);
        return 1;
    }
    prog_print_color("/dev/", 0x00FFFFFF, 0);
    prog_print_color(dev.name, 0x00FFFF00, 0);
    prog_print_color(": ", 0x00FFFFFF, 0);
    char sbuf[32]; int sn = 0;
    uint64_t sc = dev.sector_count;
    if (sc == 0) { sbuf[sn++] = '0'; }
    else { char tmp[32]; int tm = 0; while (sc) { tmp[tm++] = '0' + (sc % 10); sc /= 10; } while (tm > 0) sbuf[sn++] = tmp[--tm]; }
    sbuf[sn] = '\0';
    prog_print_color(sbuf, 0x00FFFFFF, 0);
    prog_print_color(" sectors, ", 0x00FFFFFF, 0);
    sn = 0;
    uint64_t mb = dev.sector_count / 2048;
    if (mb == 0) { sbuf[sn++] = '0'; }
    else { char tmp[32]; int tm = 0; while (mb) { tmp[tm++] = '0' + (mb % 10); mb /= 10; } while (tm > 0) sbuf[sn++] = tmp[--tm]; }
    sbuf[sn] = '\0';
    prog_print_color(sbuf, 0x00FFFFFF, 0);
    prog_print_color(" MB\n", 0x00FFFFFF, 0);
    for (int p = 0; p < MAX_PARTITIONS; p++) {
        if (!dev.partitions[p].used) continue;
        prog_print_color("  /dev/", 0x00FFFFFF, 0);
        prog_print_color(dev.name, 0x00FFFF00, 0);
        char digit[2] = {'0' + p + 1, 0};
        prog_print_color(digit, 0x00FFFF00, 0);
        prog_print_color(": ", 0x00FFFFFF, 0);
        sn = 0;
        uint64_t ps = dev.partitions[p].sector_count;
        if (ps == 0) { sbuf[sn++] = '0'; }
        else { char tmp[32]; int tm = 0; while (ps) { tmp[tm++] = '0' + (ps % 10); ps /= 10; } while (tm > 0) sbuf[sn++] = tmp[--tm]; }
        sbuf[sn] = '\0';
        prog_print_color(sbuf, 0x00FFFFFF, 0);
        prog_print_color(" sectors\n", 0x00FFFFFF, 0);
    }
    return 0;
}

int rdisk_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: rdisk <lba>\n", 0x00FF0000, 0); return 1; }
    uint64_t lba = 0;
    for (int i = 0; argv[1][i]; i++) lba = lba * 10 + (argv[1][i] - '0');
    char sector[512];
    struct blk_io io = { lba, 1, sector };
    if (hda_ioctl(BLK_READ_SECTOR, &io) < 0) { prog_print_color("Read error\n", 0x00FF0000, 0); return 1; }
    for (int i = 0; i < 512 && i < 256; i++) {
        if (sector[i] >= 0x20 && sector[i] < 0x7F) prog_putc(sector[i], 0x00FFFFFF, 0);
        else prog_putc('.', 0x00808080, 0);
    }
    prog_newline();
    return 0;
}

int wdisk_main(int argc, char **argv) {
    if (argc < 3) { prog_print_color("Usage: wdisk <lba> <text>\n", 0x00FF0000, 0); return 1; }
    uint64_t lba = 0;
    for (int i = 0; argv[1][i]; i++) lba = lba * 10 + (argv[1][i] - '0');
    char sector[512];
    for (int i = 0; i < 512; i++) sector[i] = 0;
    int pos = 0;
    for (int i = 2; i < argc; i++) {
        for (int j = 0; argv[i][j] && pos < 511; j++) sector[pos++] = argv[i][j];
        if (i < argc - 1 && pos < 511) sector[pos++] = ' ';
    }
    struct blk_io io = { lba, 1, sector };
    if (hda_ioctl(BLK_WRITE_SECTOR, &io) < 0) { prog_print_color("Write error\n", 0x00FF0000, 0); return 1; }
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}

int mount_main(int argc, char **argv) {
    if (argc < 3) {
        prog_print_color("Usage: mount <device> <path>\n", 0x00FF0000, 0);
        prog_print_color("Mounted:\n", 0x00FFFFFF, 0);
        int mc = mount_count();
        for (int i = 0; i < mc; i++) {
            prog_print_color("  ", 0x00FFFFFF, 0);
            prog_print_color(mount_device(i), 0x00FFFF00, 0);
            prog_print_color(" on ", 0x00FFFFFF, 0);
            prog_print_color(mount_point(i), 0x0000FFFF, 0);
            prog_newline();
        }
        if (mc == 0) prog_print_color("  (none)\n", 0x00FFFFFF, 0);
        return 0;
    }
    mkdir(argv[2]);
    if (mount(argv[1], argv[2]) < 0) { prog_print_color("Mount failed\n", 0x00FF0000, 0); return 1; }
    prog_print_color("Mounted ", 0x0000FF00, 0);
    prog_print_color(argv[1], 0x00FFFF00, 0);
    prog_print_color(" on ", 0x0000FF00, 0);
    prog_print_color(argv[2], 0x0000FFFF, 0);
    prog_newline();
    return 0;
}

int umount_main(int argc, char **argv) {
    if (argc < 2) { prog_print_color("Usage: umount <path>\n", 0x00FF0000, 0); return 1; }
    if (umount(argv[1]) < 0) { prog_print_color("Umount failed\n", 0x00FF0000, 0); return 1; }
    prog_print_color("OK\n", 0x0000FF00, 0);
    return 0;
}
