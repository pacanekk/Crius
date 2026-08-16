#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"
#include "drivers/serial.h"

static uint64_t hhdm = 0;

static uint64_t *alloc_page_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) return NULL;
    uint64_t *table = (uint64_t *)(hhdm + phys);
    memset(table, 0, PAGE_SIZE);
    return table;
}

static uint64_t table_phys(uint64_t *table) {
    return (uint64_t)table - hhdm;
}

static uint64_t *get_table(uint64_t phys) {
    return (uint64_t *)(hhdm + phys);
}

void vmm_init(uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
}

uint64_t vmm_get_hhdm(void) {
    return hhdm;
}

uint64_t vmm_current_pml4(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & ~0xFFFUL;
}

void vmm_switch_pml4(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t vmm_create_pml4(void) {
    uint64_t *pml4 = alloc_page_table();
    if (!pml4) return 0;
    return table_phys(pml4);
}

int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pml4 = get_table(pml4_phys);

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pdpt;
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        pdpt = alloc_page_table();
        if (!pdpt) return -1;
        pml4[pml4_idx] = table_phys(pdpt) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pdpt = get_table(pml4[pml4_idx] & ~0xFFFUL);
    }

    uint64_t *pd;
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        pd = alloc_page_table();
        if (!pd) return -1;
        pdpt[pdpt_idx] = table_phys(pd) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pd = get_table(pdpt[pdpt_idx] & ~0xFFFUL);
    }

    uint64_t *pt;
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        pt = alloc_page_table();
        if (!pt) return -1;
        pd[pd_idx] = table_phys(pt) | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pt = get_table(pd[pd_idx] & ~0xFFFUL);
    }

    if (pt[pt_idx] & PAGE_PRESENT) {
        return -1;
    }
    pt[pt_idx] = (phys & ~(0xFFFUL | PAGE_NX)) | flags;
    return 0;
}

int vmm_map_range(uint64_t pml4_phys, uint64_t virt, uint64_t phys, size_t count, uint64_t flags) {
    for (size_t i = 0; i < count; i++) {
        if (vmm_map_page(pml4_phys, virt + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) < 0)
            return -1;
    }
    return 0;
}

uint64_t vmm_get_phys(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = get_table(pml4_phys);

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = get_table(pml4[pml4_idx] & ~0xFFFUL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pd = get_table(pdpt[pdpt_idx] & ~0xFFFUL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    uint64_t *pt = get_table(pd[pd_idx] & ~0xFFFUL);

    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return pt[pt_idx] & ~(0xFFFUL | PAGE_NX);
}

void vmm_map_kernel(uint64_t pml4_phys) {
    uint64_t cr3 = vmm_current_pml4();
    uint64_t *kern_pml4 = get_table(cr3);

    for (int i = 256; i < 512; i++) {
        if (kern_pml4[i] & PAGE_PRESENT) {
            uint64_t *dst_pml4 = get_table(pml4_phys);
            dst_pml4[i] = kern_pml4[i];
        }
    }
}

static int copy_pt(uint64_t *dst_pt, uint64_t *src_pt) {
    for (int i = 0; i < 512; i++) {
        if (!(src_pt[i] & PAGE_PRESENT)) continue;
        uint64_t old_phys = src_pt[i] & ~(0xFFFUL | PAGE_NX);
        uint64_t flags = src_pt[i] & (0xFFFUL | PAGE_NX);
        /* COW: share the physical page, mark both sides read-only + COW */
        dst_pt[i] = old_phys | (flags & ~PAGE_WRITABLE) | PAGE_COW;
        src_pt[i] = old_phys | (flags & ~PAGE_WRITABLE) | PAGE_COW;
        pmm_incref(old_phys);
    }
    return 0;
}

static int copy_pd(uint64_t *dst_pd, uint64_t *src_pd) {
    for (int i = 0; i < 512; i++) {
        if (!(src_pd[i] & PAGE_PRESENT)) continue;
        uint64_t *src_pt = get_table(src_pd[i] & ~0xFFFUL);
        uint64_t *dst_pt = alloc_page_table();
        if (!dst_pt) return -1;
        if (copy_pt(dst_pt, src_pt) < 0) {
            /* Link dst_pt so vmm_free_user_pages can reach and free it
             * along with any pages already copied into it. */
            dst_pd[i] = table_phys(dst_pt) | (src_pd[i] & 0xFFFUL);
            return -1;
        }
        dst_pd[i] = table_phys(dst_pt) | (src_pd[i] & 0xFFFUL);
    }
    return 0;
}

static int copy_pdpt(uint64_t *dst_pdpt, uint64_t *src_pdpt) {
    for (int i = 0; i < 512; i++) {
        if (!(src_pdpt[i] & PAGE_PRESENT)) continue;
        uint64_t *src_pd = get_table(src_pdpt[i] & ~0xFFFUL);
        uint64_t *dst_pd = alloc_page_table();
        if (!dst_pd) return -1;
        if (copy_pd(dst_pd, src_pd) < 0) {
            /* Link dst_pd so vmm_free_user_pages can reach and free it. */
            dst_pdpt[i] = table_phys(dst_pd) | (src_pdpt[i] & 0xFFFUL);
            return -1;
        }
        dst_pdpt[i] = table_phys(dst_pd) | (src_pdpt[i] & 0xFFFUL);
    }
    return 0;
}

int vmm_copy_userspace(uint64_t dst_pml4, uint64_t src_pml4) {
    uint64_t *src = get_table(src_pml4);
    uint64_t *dst = get_table(dst_pml4);
    for (int i = 0; i < 256; i++) {
        if (!(src[i] & PAGE_PRESENT)) continue;
        uint64_t *src_pdpt = get_table(src[i] & ~0xFFFUL);
        uint64_t *dst_pdpt = alloc_page_table();
        if (!dst_pdpt) return -1;
        if (copy_pdpt(dst_pdpt, src_pdpt) < 0) {
            /* Link dst_pdpt so vmm_free_user_pages can reach and free it. */
            dst[i] = table_phys(dst_pdpt) | (src[i] & 0xFFFUL);
            return -1;
        }
        dst[i] = table_phys(dst_pdpt) | (src[i] & 0xFFFUL);
    }
    return 0;
}

static void free_user_pt(uint64_t *pt) {
    for (int i = 0; i < 512; i++) {
        if (pt[i] & PAGE_PRESENT) {
            pmm_free_page(pt[i] & ~(0xFFFUL | PAGE_NX));
        }
    }
}

static void free_user_pd(uint64_t *pd) {
    for (int i = 0; i < 512; i++) {
        if (!(pd[i] & PAGE_PRESENT)) continue;
        uint64_t *pt = get_table(pd[i] & ~0xFFFUL);
        free_user_pt(pt);
        pmm_free_page(pd[i] & ~0xFFFUL);
    }
}

static void free_user_pdpt(uint64_t *pdpt) {
    for (int i = 0; i < 512; i++) {
        if (!(pdpt[i] & PAGE_PRESENT)) continue;
        uint64_t *pd = get_table(pdpt[i] & ~0xFFFUL);
        free_user_pd(pd);
        pmm_free_page(pdpt[i] & ~0xFFFUL);
    }
}

void vmm_free_user_pages(uint64_t pml4_phys) {
    uint64_t *pml4 = get_table(pml4_phys);
    for (int i = 0; i < 256; i++) {
        if (!(pml4[i] & PAGE_PRESENT)) continue;
        uint64_t *pdpt = get_table(pml4[i] & ~0xFFFUL);
        free_user_pdpt(pdpt);
        pmm_free_page(pml4[i] & ~0xFFFUL);
        pml4[i] = 0;
    }
}

int vmm_handle_cow_fault(uint64_t pml4_phys, uint64_t virt) {
    uint64_t *pml4 = get_table(pml4_phys);

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return -1;
    uint64_t *pdpt = get_table(pml4[pml4_idx] & ~0xFFFUL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return -1;
    uint64_t *pd = get_table(pdpt[pdpt_idx] & ~0xFFFUL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return -1;
    uint64_t *pt = get_table(pd[pd_idx] & ~0xFFFUL);

    if (!(pt[pt_idx] & PAGE_PRESENT)) return -1;
    if (!(pt[pt_idx] & PAGE_COW)) return -1;  /* not a COW page */

    uint64_t old_phys = pt[pt_idx] & ~(0xFFFUL | PAGE_NX);
    uint64_t flags = pt[pt_idx] & (0xFFFUL | PAGE_NX);

    if (pmm_get_refcount(old_phys) == 1) {
        /* Only owner - just make it writable again, clear COW */
        pt[pt_idx] = old_phys | (flags & ~PAGE_COW) | PAGE_WRITABLE;
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
        return 0;
    }

    /* Shared page - copy to a new physical page */
    uint64_t new_phys = pmm_alloc_page();
    if (new_phys == 0) return -1;

    memcpy((void *)(hhdm + new_phys), (void *)(hhdm + old_phys), PAGE_SIZE);
    pmm_free_page(old_phys);  /* decrement refcount on old page */
    pt[pt_idx] = new_phys | (flags & ~PAGE_COW) | PAGE_WRITABLE;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}
