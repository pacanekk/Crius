#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

void fb_init(struct limine_framebuffer *framebuffer);
void fb_clear(uint32_t color);
void fb_scroll(int lines, uint32_t bg);
void fb_putc(char c, uint32_t fg, uint32_t bg);
void fb_puts(const char *str, uint32_t fg, uint32_t bg);
void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg);
int fb_get_x(void);
void fb_set_x(int x);
int fb_get_y(void);
void fb_set_y(int y);
int fb_get_width(void);
int fb_get_height(void);

/* Whether a framebuffer was successfully initialized at boot */
void fb_set_available(int available);
int  fb_is_available(void);

/* Device callbacks for /dev/stdout, /dev/fb0, /dev/fbinfo */
int fb_dev_write(const char *data, size_t len);
int fb_dev_info_read(char *buf, size_t bufsize);
int fb_dev_present(void);

#endif
