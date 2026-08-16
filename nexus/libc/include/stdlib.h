#ifndef NEXUS_STDLIB_H
#define NEXUS_STDLIB_H

#include <stddef.h>

static inline int atoi(const char *s) {
    int sign = 1, val = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val * sign;
}

static inline char *itoa(int value, char *buf, int base) {
    if (base < 2 || base > 36) return buf;
    char tmp[32];
    int i = 0;
    int neg = 0;
    if (value < 0 && base == 10) { neg = 1; value = -value; }
    if (value == 0) { tmp[i++] = '0'; }
    while (value > 0) {
        int d = value % base;
        tmp[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        value /= base;
    }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

#endif
