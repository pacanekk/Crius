#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "mm/kmalloc.h"
#include "mm/pmm.h"

#define PAGE_SIZE 4096
#define MAX_PAGE_RUNS 256
#define MIN_BLOCK (sizeof(struct block) + sizeof(struct footer) + 16)

struct block {
    size_t size;
    bool free;
    struct block *next;
    struct block *prev;
};

struct footer {
    size_t size;
};

struct page_run {
    uint64_t phys;
    size_t page_count;
    void *virt;
};

static uint64_t hhdm;
static struct page_run page_runs[MAX_PAGE_RUNS];
static int page_run_count;
static struct block *free_list;

static struct block *header_from_ptr(void *ptr) {
    return (struct block *)((uint8_t *)ptr - sizeof(struct block));
}

static void *ptr_from_header(struct block *b) {
    return (uint8_t *)b + sizeof(struct block);
}

static struct footer *footer_from_block(struct block *b) {
    return (struct footer *)((uint8_t *)b + b->size - sizeof(struct footer));
}

static void set_footer(struct block *b) {
    struct footer *f = footer_from_block(b);
    f->size = b->size;
}

static void free_list_insert(struct block *b) {
    b->next = free_list;
    b->prev = NULL;
    if (free_list)
        free_list->prev = b;
    free_list = b;
}

static void free_list_remove(struct block *b) {
    if (b->prev)
        b->prev->next = b->next;
    else
        free_list = b->next;
    if (b->next)
        b->next->prev = b->prev;
    b->next = NULL;
    b->prev = NULL;
}

static struct block *find_fit(size_t total) {
    for (struct block *b = free_list; b; b = b->next) {
        if (b->free && b->size >= total)
            return b;
    }
    return NULL;
}

static void split_block(struct block *b, size_t total) {
    if (b->size < total + MIN_BLOCK)
        return;
    struct block *new_block = (struct block *)((uint8_t *)b + total);
    new_block->size = b->size - total;
    new_block->free = true;
    new_block->next = NULL;
    new_block->prev = NULL;
    b->size = total;
    set_footer(new_block);
    set_footer(b);
    free_list_insert(new_block);
}

static int new_page_run(size_t needed) {
    if (page_run_count >= MAX_PAGE_RUNS)
        return -1;

    size_t pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    uint64_t phys = pmm_alloc_pages(pages);
    if (phys == 0) {
        phys = pmm_alloc_page();
        if (phys == 0) return -1;
        pages = 1;
    }

    void *virt = (void *)(hhdm + phys);
    struct block *first = (struct block *)virt;
    first->size = pages * PAGE_SIZE;
    first->free = true;
    first->next = NULL;
    first->prev = NULL;
    set_footer(first);

    page_runs[page_run_count].phys = phys;
    page_runs[page_run_count].page_count = pages;
    page_runs[page_run_count].virt = virt;
    page_run_count++;

    free_list_insert(first);
    return 0;
}

void kmalloc_init(uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
    page_run_count = 0;
    free_list = NULL;
    new_page_run(PAGE_SIZE);
}

void *kmalloc(size_t size) {
    if (size == 0)
        return NULL;

    size_t total = size + sizeof(struct block) + sizeof(struct footer);
    total = (total + 7) & ~7;
    if (total < MIN_BLOCK)
        total = MIN_BLOCK;

    struct block *b = find_fit(total);
    if (!b) {
        if (new_page_run(total) < 0)
            return NULL;
        b = find_fit(total);
        if (!b)
            return NULL;
    }

    free_list_remove(b);
    split_block(b, total);
    b->free = false;
    return ptr_from_header(b);
}

static struct page_run *find_page_run(struct block *b) {
    for (int i = 0; i < page_run_count; i++) {
        uint8_t *start = (uint8_t *)page_runs[i].virt;
        uint8_t *end = start + page_runs[i].page_count * PAGE_SIZE;
        if ((uint8_t *)b >= start && (uint8_t *)b < end)
            return &page_runs[i];
    }
    return NULL;
}

static struct block *merge_forward(struct block *b) {
    for (;;) {
        struct page_run *run = find_page_run(b);
        if (!run) break;
        uint8_t *end = (uint8_t *)run->virt + run->page_count * PAGE_SIZE;
        uint8_t *next = (uint8_t *)b + b->size;
        if (next >= end) break;

        struct block *n = (struct block *)next;
        if (!n->free) break;
        free_list_remove(n);
        b->size += n->size;
        set_footer(b);
    }
    return b;
}

static struct block *merge_backward(struct block *b) {
    for (;;) {
        struct page_run *run = find_page_run(b);
        if (!run) break;
        uint8_t *start = (uint8_t *)run->virt;
        if ((uint8_t *)b <= start + sizeof(struct footer)) break;

        struct footer *prev_footer = (struct footer *)((uint8_t *)b - sizeof(struct footer));
        struct block *p = (struct block *)((uint8_t *)b - prev_footer->size);
        if ((uint8_t *)p < start) break;
        if (!p->free) break;

        free_list_remove(p);
        p->size += b->size;
        set_footer(p);
        free_list_remove(b);
        b = p;
    }
    return b;
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    struct block *b = header_from_ptr(ptr);
    struct page_run *run = find_page_run(b);
    if (!run || b->free)
        return;

    b->free = true;
    free_list_insert(b);
    b = merge_forward(b);
    set_footer(b);
    b = merge_backward(b);

    if (b->size == run->page_count * PAGE_SIZE && b == run->virt) {
        free_list_remove(b);
        pmm_free_pages(run->phys, run->page_count);
        run->phys = page_runs[page_run_count - 1].phys;
        run->page_count = page_runs[page_run_count - 1].page_count;
        run->virt = page_runs[page_run_count - 1].virt;
        page_run_count--;
    }
}
