#ifndef FB_INTERNAL_H
#define FB_INTERNAL_H

#include <stdint.h>
#include "drivers/framebuffer.h"

extern struct limine_framebuffer *fb;
extern int fb_x, fb_y;
extern int fb_available;

/* ANSI state */
#define ANSI_MAX_PARAMS 8
extern int ansi_state;
extern int ansi_params[ANSI_MAX_PARAMS];
extern int ansi_nparams;
extern int ansi_cur_param;
extern uint32_t ansi_fg;
extern uint32_t ansi_bg;
extern int ansi_reverse;
extern int saved_fb_x, saved_fb_y;

extern const uint32_t ansi_colors[16];

void ansi_reset(void);
void ansi_sgr(void);
void ansi_csi_dispatch(char cmd);
void fb_putc_raw(char c, uint32_t fg, uint32_t bg);

#endif
