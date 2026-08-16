#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>

void kmalloc_init(uint64_t hhdm_offset);
void *kmalloc(size_t size);
void kfree(void *ptr);

#endif
