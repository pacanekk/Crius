#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *str);
void serial_hex(uint64_t val);

/* Device callback for /dev/ttyS0 */
int serial_dev_write(const char *data, size_t len);
int serial_dev_present(void);

#endif
