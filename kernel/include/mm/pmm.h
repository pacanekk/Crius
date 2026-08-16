#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

void pmm_init(struct limine_memmap_response *memmap_resp);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(size_t count);
void pmm_free_page(uint64_t phys);
void pmm_free_pages(uint64_t phys, size_t count);
void pmm_incref(uint64_t phys);
uint8_t pmm_get_refcount(uint64_t phys);
void pmm_stats(size_t *total, size_t *free);

#endif
