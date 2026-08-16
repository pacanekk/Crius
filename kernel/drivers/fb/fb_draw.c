#include <stddef.h>
#include <stdint.h>
#include "drivers/framebuffer.h"
#include "fb_internal.h"
#include "drivers/font8x8.h"

/* Shared framebuffer state */
struct limine_framebuffer *fb = NULL;
int fb_x = 0, fb_y = 0;
int fb_available = 0;

void fb_set_available(int available) { fb_available = available; }
int  fb_is_available(void) { return fb_available; }

void fb_init(struct limine_framebuffer *framebuffer) {
    fb = framebuffer;
    fb_x = 0;
    fb_y = 0;
}

static void fb_draw_pixel(int x, int y, uint32_t color) {
    if (!fb) return;
    volatile uint32_t *ptr = fb->address;
    ptr[y * (fb->pitch / 4) + x] = color;
}

void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc > 127) uc = '?';
    const unsigned char *glyph = font8x8_basic[uc - 32];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t color = (glyph[row] & (1 << col)) ? fg : bg;
            fb_draw_pixel(x + col, y + row, color);
        }
    }
}

void fb_clear(uint32_t color) {
    if (!fb) return;
    for (size_t y = 0; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            fb_draw_pixel(x, y, color);
        }
    }
    fb_x = 0;
    fb_y = 0;
}

void fb_scroll(int lines, uint32_t bg) {
    if (!fb || lines <= 0) return;
    int pitch_px = fb->pitch / 4;
    volatile uint32_t *ptr = fb->address;

    for (size_t y = 0; y + lines < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            ptr[y * pitch_px + x] = ptr[(y + lines) * pitch_px + x];
        }
    }
    for (size_t y = fb->height - lines; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            ptr[y * pitch_px + x] = bg;
        }
    }

    /* Adjust saved cursor position for scroll */
    saved_fb_y -= lines;
    if (saved_fb_y < 0) saved_fb_y = 0;
}

/* Raw putc - no ANSI parsing, uses provided colors */
void fb_putc_raw(char c, uint32_t fg, uint32_t bg) {
    if (!fb) return;
    if (ansi_reverse) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if (c == '\n') {
        fb_x = 0;
        fb_y += 8;
        if (fb_y + 8 > (int)fb->height) {
            fb_scroll(8, bg);
            fb_y = (int)fb->height - 8;
        }
        return;
    }
    if (c == '\b') {
        if (fb_x >= 8)
            fb_x -= 8;
        return;
    }
    if (c == '\r') {
        fb_x = 0;
        return;
    }
    if (c == '\t') {
        fb_x = (fb_x + 8 * 4) & ~(8 * 4 - 1);
        if (fb_x + 8 > (int)fb->width) { fb_x = 0; fb_y += 8; }
        return;
    }
    fb_draw_char(c, fb_x, fb_y, fg, bg);
    fb_x += 8;
    if (fb_x + 8 > (int)fb->width) {
        fb_x = 0;
        fb_y += 8;
    }
    if (fb_y + 8 > (int)fb->height) {
        fb_scroll(8, bg);
        fb_y = (int)fb->height - 8;
    }
}

int fb_get_x(void) {
    return fb_x;
}

void fb_set_x(int x) {
    fb_x = x;
}

int fb_get_y(void) {
    return fb_y;
}

void fb_set_y(int y) {
    fb_y = y;
}

int fb_get_width(void) {
    return fb ? (int)fb->width : 0;
}

int fb_get_height(void) {
    return fb ? (int)fb->height : 0;
}
