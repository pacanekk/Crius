#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

char kb_read(void);
unsigned char kb_buf_pop(void);
int kb_buf_pending(void);
void irq_handler(uint64_t vector);

/* Shared keyboard buffer */
void kb_buf_push(unsigned char c);

/* USB keyboard state (set by xhci.c) */
extern int usb_kbd_present;

/* USB keyboard polling hook (implemented in xhci.c) */
int usb_kbd_poll(void);
void usb_kbd_tick(void);

/* Device callbacks for /dev/stdin, /dev/kbd */
int kbd_dev_read(char *buf, size_t bufsize);
int kbd_dev_present(void);

#endif
