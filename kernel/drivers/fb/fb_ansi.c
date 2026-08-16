#include <stdint.h>
#include "drivers/framebuffer.h"
#include "fb_internal.h"

/* ANSI escape sequence parser state */
int ansi_state = 0;       /* 0=normal, 1=ESC, 2=CSI */
int ansi_params[ANSI_MAX_PARAMS];
int ansi_nparams = 0;
int ansi_cur_param = 0;
uint32_t ansi_fg = 0x00FFFFFF;
uint32_t ansi_bg = 0x00000000;
int ansi_reverse = 0;
int saved_fb_x = 0, saved_fb_y = 0;

/* ANSI color table - maps 0-15 to RGB */
const uint32_t ansi_colors[16] = {
    0x00000000, /* black */
    0x00AA0000, /* red */
    0x0000AA00, /* green */
    0x00AA5500, /* yellow */
    0x000000AA, /* blue */
    0x00AA00AA, /* magenta */
    0x0000AAAA, /* cyan */
    0x00AAAAAA, /* white */
    0x00555555, /* bright black */
    0x00FF5555, /* bright red */
    0x0055FF55, /* bright green */
    0x00FFFF55, /* bright yellow */
    0x005555FF, /* bright blue */
    0x00FF55FF, /* bright magenta */
    0x0055FFFF, /* bright cyan */
    0x00FFFFFF, /* bright white */
};

void ansi_reset(void) {
    ansi_state = 0;
    ansi_nparams = 0;
    ansi_cur_param = 0;
    for (int i = 0; i < ANSI_MAX_PARAMS; i++) ansi_params[i] = 0;
}

void ansi_sgr(void) {
    if (ansi_nparams == 0) {
        ansi_fg = 0x00FFFFFF;
        ansi_bg = 0x00000000;
        ansi_reverse = 0;
        return;
    }
    for (int i = 0; i < ansi_nparams; i++) {
        int p = ansi_params[i];
        if (p == 0) {
            ansi_fg = 0x00FFFFFF;
            ansi_bg = 0x00000000;
            ansi_reverse = 0;
        } else if (p == 7) {
            ansi_reverse = 1;
        } else if (p == 27) {
            ansi_reverse = 0;
        } else if (p >= 30 && p <= 37) {
            ansi_fg = ansi_colors[p - 30];
        } else if (p >= 40 && p <= 47) {
            ansi_bg = ansi_colors[p - 40];
        } else if (p >= 90 && p <= 97) {
            ansi_fg = ansi_colors[p - 90 + 8];
        } else if (p >= 100 && p <= 107) {
            ansi_bg = ansi_colors[p - 100 + 8];
        }
    }
}

void ansi_csi_dispatch(char cmd) {
    int p0 = ansi_params[0];
    int p1 = ansi_params[1];
    if (ansi_nparams == 0) p0 = 0;
    if (ansi_nparams < 2) p1 = 0;

    switch (cmd) {
    case 'H':   /* cursor position: row;col (1-based) */
    case 'f':
        fb_y = (p0 > 0 ? p0 - 1 : 0) * 8;
        fb_x = (p1 > 0 ? p1 - 1 : 0) * 8;
        if (fb_x < 0) fb_x = 0;
        if (fb_y < 0) fb_y = 0;
        break;
    case 'A':   /* cursor up */
        fb_y -= p0 * 8;
        if (fb_y < 0) fb_y = 0;
        break;
    case 'B':   /* cursor down */
        fb_y += p0 * 8;
        break;
    case 'C':   /* cursor forward */
        fb_x += p0 * 8;
        break;
    case 'D':   /* cursor back */
        fb_x -= p0 * 8;
        if (fb_x < 0) fb_x = 0;
        break;
    case 'J':   /* erase display */
        if (p0 == 0) {
            /* erase from cursor to end */
            if (fb) {
                int pitch_px = fb->pitch / 4;
                volatile uint32_t *ptr = fb->address;
                for (int y = fb_y; y < fb_y + 8 && y < (int)fb->height; y++) {
                    int start_x = (y == fb_y) ? fb_x : 0;
                    for (int x = start_x; x < (int)fb->width; x++)
                        ptr[y * pitch_px + x] = ansi_bg;
                }
                for (int y = fb_y + 8; y < (int)fb->height; y++)
                    for (int x = 0; x < (int)fb->width; x++)
                        ptr[y * pitch_px + x] = ansi_bg;
            }
        } else if (p0 == 2 || p0 == 3) {
            fb_clear(ansi_bg);
        }
        break;
    case 'K':   /* erase line */
        if (fb) {
            int pitch_px = fb->pitch / 4;
            volatile uint32_t *ptr = fb->address;
            int start_x, end_x;
            if (p0 == 1) { start_x = 0; end_x = fb_x; }
            else if (p0 == 2) { start_x = 0; end_x = fb->width; }
            else { start_x = fb_x; end_x = fb->width; }
            for (int y = fb_y; y < fb_y + 8 && y < (int)fb->height; y++)
                for (int x = start_x; x < end_x; x++)
                    ptr[y * pitch_px + x] = ansi_bg;
        }
        break;
    case 'm':   /* SGR - colors */
        ansi_sgr();
        break;
    default:
        break;
    }
}

/* ANSI-aware putc - parses escape sequences and uses internal color state.
 * This is what write(1,...) / dev_stdout_write calls. */
void fb_putc(char c, uint32_t fg, uint32_t bg) {
    (void)fg; (void)bg;
    if (!fb) return;

    /* ANSI escape sequence parser */
    if (ansi_state == 0) {
        if (c == 0x1B) {
            ansi_state = 1;
            return;
        }
        fb_putc_raw(c, ansi_fg, ansi_bg);
        return;
    }

    if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_nparams = 0;
            ansi_cur_param = 0;
            for (int i = 0; i < ANSI_MAX_PARAMS; i++) ansi_params[i] = 0;
            return;
        }
        if (c == '7') {
            saved_fb_x = fb_x;
            saved_fb_y = fb_y;
            ansi_reset();
            return;
        }
        if (c == '8') {
            fb_x = saved_fb_x;
            fb_y = saved_fb_y;
            ansi_reset();
            return;
        }
        /* Not a CSI - ignore */
        ansi_reset();
        return;
    }

    if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            ansi_cur_param = ansi_cur_param * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (ansi_nparams < ANSI_MAX_PARAMS)
                ansi_params[ansi_nparams++] = ansi_cur_param;
            ansi_cur_param = 0;
            return;
        }
        /* End of sequence - dispatch */
        if (ansi_nparams < ANSI_MAX_PARAMS)
            ansi_params[ansi_nparams++] = ansi_cur_param;
        ansi_csi_dispatch(c);
        ansi_reset();
        return;
    }
}

void fb_puts(const char *str, uint32_t fg, uint32_t bg) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        fb_putc(str[i], fg, bg);
    }
}
