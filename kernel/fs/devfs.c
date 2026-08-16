#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs/ramfs.h"
#include "drivers/framebuffer.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/block_device.h"

/* ===== /dev/null (pure VFS device, no driver) ===== */

static int dev_null_write(const char *data, size_t len) {
    (void)data;
    return (int)len;
}

static int dev_null_read(char *buf, size_t bufsize) {
    (void)bufsize;
    if (bufsize > 0) buf[0] = '\0';
    return 0;
}

/* ===== devfs_init - called from vfs_init ===== */

void devfs_init(void) {
    ramfs_mkdir("/dev");

    ramfs_create_dev("/dev/null", dev_null_read, dev_null_write);

    if (fb_dev_present()) {
        ramfs_create_dev("/dev/stdout", NULL, fb_dev_write);
        ramfs_create_dev("/dev/stderr", NULL, fb_dev_write);
        ramfs_create_dev("/dev/fb0", NULL, fb_dev_write);
        ramfs_create_dev("/dev/fbinfo", fb_dev_info_read, NULL);
    }

    if (kbd_dev_present()) {
        ramfs_create_dev_stream("/dev/stdin", kbd_dev_read, NULL);
        ramfs_create_dev("/dev/kbd", kbd_dev_read, NULL);
    }

    if (serial_dev_present()) {
        ramfs_create_dev("/dev/ttyS0", NULL, serial_dev_write);
    }

    if (blk_dev_present()) {
        ramfs_create_dev_ioctl("/dev/hda", blk_dev_read, blk_dev_write, blk_dev_ioctl);
    }
}
