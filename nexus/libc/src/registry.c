/*
 * Nexus libc - helper functions for userspace programs.
 *
 * All output goes through write(1, ...) - no direct console access.
 * Colors are implemented via ANSI escape sequences.
 */

#include <unistd.h>
#include <stdint.h>
#include <string.h>

/* Forward declarations from api.h (kept for compatibility) */
#define PROG_MAX_ARGS 16
typedef int (*prog_main_t)(int argc, char **argv);

void prog_print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

/* Map an RGB color to the nearest ANSI 16-color index.
 * Returns -1 for white/default (no escape needed). */
static int rgb_to_ansi(uint32_t rgb) {
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8) & 0xFF;
    int b = rgb & 0xFF;

    /* Bright vs normal threshold */
    int bright = (r >= 128 || g >= 128 || b >= 128);

    /* Determine dominant channel(s) */
    int ri = r >= 128 ? 1 : (r >= 64 ? 1 : 0);
    int gi = g >= 128 ? 1 : (g >= 64 ? 1 : 0);
    int bi = b >= 128 ? 1 : (b >= 64 ? 1 : 0);

    if (!ri && !gi && !bi) return 0;           /* black */
    if (ri && gi && bi) return bright ? 15 : 7; /* white */
    if (ri && gi && !bi) return bright ? 11 : 3; /* yellow/brown */
    if (ri && !gi && bi) return bright ? 13 : 5; /* magenta */
    if (!ri && gi && bi) return bright ? 14 : 6; /* cyan */
    if (ri) return bright ? 9 : 1;               /* red */
    if (gi) return bright ? 10 : 2;              /* green */
    if (bi) return bright ? 12 : 4;              /* blue */
    return -1;
}

static void write_ansi_color(int code) {
    char buf[8];
    int n = 0;
    buf[n++] = '\033';
    buf[n++] = '[';
    if (code < 0) {
        buf[n++] = '0';
    } else if (code < 10) {
        buf[n++] = '0' + code;
    } else {
        buf[n++] = '0' + (code / 10);
        buf[n++] = '0' + (code % 10);
    }
    buf[n++] = 'm';
    write(1, buf, n);
}

void prog_print_color(const char *s, uint32_t fg, uint32_t bg) {
    (void)bg;
    int color = rgb_to_ansi(fg);
    if (color >= 0 && color != 7) {
        write_ansi_color(color);
        write(1, s, strlen(s));
        write_ansi_color(-1); /* reset */
    } else {
        write(1, s, strlen(s));
    }
}

void prog_putc(char c, uint32_t fg, uint32_t bg) {
    (void)fg;
    (void)bg;
    write(1, &c, 1);
}

void prog_newline(void) {
    char nl = '\n';
    write(1, &nl, 1);
}

/* Editor stubs - real implementation in editor.c (linked into shell).
 * Marked weak so editor.c overrides them when linked together. */
__attribute__((weak)) int edit_active(void) { return 0; }
__attribute__((weak)) void edit_handle_char(unsigned char c) { (void)c; }
