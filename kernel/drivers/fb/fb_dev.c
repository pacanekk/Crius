#include <stddef.h>
#include <stdint.h>
#include "drivers/framebuffer.h"
#include "fb_internal.h"

/* ===== Device callbacks ===== */

static void fb_put_str(char *buf, int *pos, int bufsize, const char *s) {
    while (*s && *pos < bufsize - 1) buf[(*pos)++] = *s++;
}

static void fb_put_uint(char *buf, int *pos, int bufsize, uint64_t val) {
    if (val == 0) { if (*pos < bufsize - 1) buf[(*pos)++] = '0'; return; }
    char tmp[20]; int tm = 0;
    while (val) { tmp[tm++] = '0' + (val % 10); val /= 10; }
    while (tm > 0 && *pos < bufsize - 1) buf[(*pos)++] = tmp[--tm];
}

int fb_dev_write(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        fb_putc(data[i], 0x00FFFFFF, 0x00000000);
    return (int)len;
}

int fb_dev_info_read(char *buf, size_t bufsize) {
    int pos = 0;
    fb_put_str(buf, &pos, bufsize, "width: ");
    fb_put_uint(buf, &pos, bufsize, (uint64_t)fb_get_width());
    fb_put_str(buf, &pos, bufsize, "\nheight: ");
    fb_put_uint(buf, &pos, bufsize, (uint64_t)fb_get_height());
    fb_put_str(buf, &pos, bufsize, "\n");
    buf[pos] = '\0';
    return pos;
}

int fb_dev_present(void) {
    return (fb_get_width() > 0 && fb_get_height() > 0);
}
