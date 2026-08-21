#include <stddef.h>
#include <stdint.h>
#include "drivers/serial.h"
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
    if (fb && fb->bpp != 24 && fb->bpp != 32) {
        serial_puts("fb: unsupported bpp ");
        serial_hex(fb->bpp);
        serial_puts("\n");
        fb = NULL;
    }
}

static uint32_t fb_color_to_pixel(uint32_t color) {
    if (!fb) return 0;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint32_t pixel = 0;
    if (fb->red_mask_size)
        pixel |= (((uint32_t)r >> (8 - fb->red_mask_size)) << fb->red_mask_shift);
    if (fb->green_mask_size)
        pixel |= (((uint32_t)g >> (8 - fb->green_mask_size)) << fb->green_mask_shift);
    if (fb->blue_mask_size)
        pixel |= (((uint32_t)b >> (8 - fb->blue_mask_size)) << fb->blue_mask_shift);
    return pixel;
}

static uint32_t fb_pixel_to_color(uint32_t pixel) {
    if (!fb) return 0;
    uint32_t r = 0, g = 0, b = 0;
    uint32_t mask;
    if (fb->red_mask_size) {
        mask = (1u << fb->red_mask_size) - 1;
        r = (pixel >> fb->red_mask_shift) & mask;
        r <<= (8 - fb->red_mask_size);
    }
    if (fb->green_mask_size) {
        mask = (1u << fb->green_mask_size) - 1;
        g = (pixel >> fb->green_mask_shift) & mask;
        g <<= (8 - fb->green_mask_size);
    }
    if (fb->blue_mask_size) {
        mask = (1u << fb->blue_mask_size) - 1;
        b = (pixel >> fb->blue_mask_shift) & mask;
        b <<= (8 - fb->blue_mask_size);
    }
    return (r << 16) | (g << 8) | b;
}

static uint32_t fb_get_pixel(int x, int y) {
    if (!fb) return 0;
    uint8_t *ptr = (uint8_t *)fb->address;
    if (fb->bpp == 32) {
        uint32_t *p = (uint32_t *)(ptr + y * fb->pitch + x * 4);
        return fb_pixel_to_color(*p);
    } else if (fb->bpp == 24) {
        uint8_t *p = ptr + y * fb->pitch + x * 3;
        uint32_t pixel = p[0] | (p[1] << 8) | (p[2] << 16);
        return fb_pixel_to_color(pixel);
    }
    return 0;
}

static void fb_draw_pixel(int x, int y, uint32_t color) {
    if (!fb) return;
    uint32_t pixel = fb_color_to_pixel(color);
    uint8_t *ptr = (uint8_t *)fb->address;
    if (fb->bpp == 32) {
        uint32_t *p = (uint32_t *)(ptr + y * fb->pitch + x * 4);
        *p = pixel;
    } else if (fb->bpp == 24) {
        uint8_t *p = ptr + y * fb->pitch + x * 3;
        p[0] = pixel & 0xFF;
        p[1] = (pixel >> 8) & 0xFF;
        p[2] = (pixel >> 16) & 0xFF;
    }
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

    for (size_t y = 0; y + lines < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            fb_draw_pixel(x, y, fb_get_pixel(x, y + lines));
        }
    }
    for (size_t y = fb->height - lines; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            fb_draw_pixel(x, y, bg);
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
