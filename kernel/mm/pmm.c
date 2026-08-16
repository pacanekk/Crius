#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "mm/pmm.h"
#include "drivers/serial.h"

#define PAGE_SIZE 4096
#define MAX_PAGES 1048576

static uint64_t pages_base;
static size_t pages_total;
static size_t pages_free;
static size_t last_scan;
static uint8_t bitmap[MAX_PAGES / 8];
static uint8_t page_refs[MAX_PAGES];

static bool bitmap_get(size_t idx) {
    return (bitmap[idx / 8] >> (idx % 8)) & 1;
}

static void bitmap_set(size_t idx, bool val) {
    if (val)
        bitmap[idx / 8] |= (1 << (idx % 8));
    else
        bitmap[idx / 8] &= ~(1 << (idx % 8));
}

void pmm_init(struct limine_memmap_response *memmap_resp) {
    for (size_t i = 0; i < sizeof(bitmap); i++)
        bitmap[i] = 0xFF;
    for (size_t i = 0; i < MAX_PAGES; i++)
        page_refs[i] = 0;

    pages_base = 0xFFFFFFFFFFFFFFFFUL;
    uint64_t lowest = 0xFFFFFFFFFFFFFFFFUL;
    uint64_t highest = 0;

    for (size_t i = 0; i < memmap_resp->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap_resp->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t base = (entry->base + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
        uint64_t end = (entry->base + entry->length) & ~((uint64_t)PAGE_SIZE - 1);

        if (base < lowest)
            lowest = base;
        if (end > highest)
            highest = end;
    }

    if (lowest == 0xFFFFFFFFFFFFFFFFUL)
        return;

    uint64_t span = highest - lowest;
    if (span / PAGE_SIZE > MAX_PAGES)
        span = (uint64_t)MAX_PAGES * PAGE_SIZE;

    pages_base = lowest;

    for (size_t i = 0; i < memmap_resp->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap_resp->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t base = (entry->base + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
        uint64_t end = (entry->base + entry->length) & ~((uint64_t)PAGE_SIZE - 1);

        for (uint64_t addr = base; addr + PAGE_SIZE <= end; addr += PAGE_SIZE) {
            size_t idx = (addr - pages_base) / PAGE_SIZE;
            if (idx < MAX_PAGES) {
                bitmap_set(idx, false);
                pages_total++;
            }
        }
    }

    pages_free = pages_total;
    last_scan = 0;
}

uint64_t pmm_alloc_page(void) {
    for (size_t i = last_scan; i < MAX_PAGES; i++) {
        if (!bitmap_get(i)) {
            bitmap_set(i, true);
            page_refs[i] = 1;
            pages_free--;
            last_scan = i + 1;
            return pages_base + i * PAGE_SIZE;
        }
    }
    for (size_t i = 0; i < last_scan; i++) {
        if (!bitmap_get(i)) {
            bitmap_set(i, true);
            page_refs[i] = 1;
            pages_free--;
            last_scan = i + 1;
            return pages_base + i * PAGE_SIZE;
        }
    }
    return 0;
}

uint64_t pmm_alloc_pages(size_t count) {
    if (count == 0)
        return 0;
    if (count == 1)
        return pmm_alloc_page();

    size_t run = 0;
    size_t start = 0;

    for (size_t i = 0; i < MAX_PAGES; i++) {
        if (!bitmap_get(i)) {
            if (run == 0)
                start = i;
            run++;
            if (run >= count) {
                for (size_t j = start; j < start + count; j++) {
                    bitmap_set(j, true);
                    page_refs[j] = 1;
                    pages_free--;
                }
                last_scan = start + count;
                return pages_base + start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

void pmm_free_page(uint64_t phys) {
    size_t idx = (phys - pages_base) / PAGE_SIZE;
    if (idx >= MAX_PAGES) return;
    if (!bitmap_get(idx)) {
        serial_puts("pmm: WARNING double-free of phys ");
        serial_hex(phys);
        serial_puts("\n");
        return;
    }
    if (page_refs[idx] > 1) {
        page_refs[idx]--;
        return;
    }
    page_refs[idx] = 0;
    bitmap_set(idx, false);
    pages_free++;
    if (idx < last_scan)
        last_scan = idx;
}

void pmm_incref(uint64_t phys) {
    size_t idx = (phys - pages_base) / PAGE_SIZE;
    if (idx >= MAX_PAGES) return;
    if (page_refs[idx] < 255)
        page_refs[idx]++;
}

uint8_t pmm_get_refcount(uint64_t phys) {
    size_t idx = (phys - pages_base) / PAGE_SIZE;
    if (idx >= MAX_PAGES) return 0;
    return page_refs[idx];
}

void pmm_free_pages(uint64_t phys, size_t count) {
    for (size_t i = 0; i < count; i++)
        pmm_free_page(phys + i * PAGE_SIZE);
}

void pmm_stats(size_t *total, size_t *free) {
    if (total) *total = pages_total;
    if (free) *free = pages_free;
}
