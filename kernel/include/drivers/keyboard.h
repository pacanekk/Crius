#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

char kb_read(void);
unsigned char kb_buf_pop(void);
int kb_buf_pending(void);
void irq_handler(uint64_t vector);

/* Device callbacks for /dev/stdin, /dev/kbd */
int kbd_dev_read(char *buf, size_t bufsize);
int kbd_dev_present(void);

#endif
