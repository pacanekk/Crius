#ifndef USERCOPY_H
#define USERCOPY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum user-space virtual address (end of lower canonical half) */
#define USER_ADDR_MAX  0x0000800000000000UL

bool validate_user_ptr(const void *ptr, size_t size);
bool validate_user_ptr_writable(const void *ptr, size_t size);
bool validate_user_string(const char *str, size_t maxlen);
int copy_from_user(void *dst, const void *src, size_t size);
int copy_to_user(void *dst, const void *src, size_t size);

#endif
