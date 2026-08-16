#include "arch/io.h"
#include "drivers/serial.h"
#include <stdint.h>

void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20));
    outb(0x3F8, (uint8_t)c);
}

void serial_puts(const char *str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        serial_putc(str[i]);
    }
}

void serial_hex(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        if (nibble == 0 && i > 60) {
            /* skip leading zeros after first */
            continue;
        }
        serial_putc(hex[nibble]);
    }
}

/* ===== Device callbacks ===== */

int serial_dev_write(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        serial_putc(data[i]);
    return (int)len;
}

int serial_dev_present(void) {
    uint8_t status = inb(0x3F8 + 5);
    return (status != 0xFF);
}
