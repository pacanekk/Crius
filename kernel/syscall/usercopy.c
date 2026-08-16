#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "syscall/usercopy.h"
#include "mm/vmm.h"

bool validate_user_ptr(const void *ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;

    if (addr == 0) return false;
    if (addr >= USER_ADDR_MAX) return false;

    uint64_t end = addr + size;
    if (end < addr) return false;          /* overflow */
    if (end > USER_ADDR_MAX) return false;
    if (size == 0) return true;

    uint64_t cr3 = vmm_current_pml4();
    uint64_t hd = vmm_get_hhdm();
    uint64_t first_page = addr & ~0xFFFUL;
    uint64_t last_page  = (end - 1) & ~0xFFFUL;

    for (uint64_t page = first_page; page <= last_page; page += PAGE_SIZE) {
        uint64_t phys = vmm_get_phys(cr3, page);
        if (phys == 0)
            return false;
        /* Check that the page is user-accessible (not kernel-only) */
        uint64_t *pml4 = (uint64_t *)(hd + cr3);
        uint64_t pml4_idx = (page >> 39) & 0x1FF;
        uint64_t pdpt_idx = (page >> 30) & 0x1FF;
        uint64_t pd_idx   = (page >> 21) & 0x1FF;
        uint64_t pt_idx   = (page >> 12) & 0x1FF;

        if (!(pml4[pml4_idx] & PAGE_PRESENT)) return false;
        uint64_t *pdpt = (uint64_t *)(hd + (pml4[pml4_idx] & ~0xFFFUL));
        if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return false;
        uint64_t *pd = (uint64_t *)(hd + (pdpt[pdpt_idx] & ~0xFFFUL));
        if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
        uint64_t *pt = (uint64_t *)(hd + (pd[pd_idx] & ~0xFFFUL));
        if (!(pt[pt_idx] & PAGE_PRESENT)) return false;
        if (!(pt[pt_idx] & PAGE_USER)) return false;
    }

    return true;
}

bool validate_user_ptr_writable(const void *ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;

    if (addr == 0) return false;
    if (addr >= USER_ADDR_MAX) return false;

    uint64_t end = addr + size;
    if (end < addr) return false;
    if (end > USER_ADDR_MAX) return false;
    if (size == 0) return true;

    uint64_t cr3 = vmm_current_pml4();
    uint64_t hd = vmm_get_hhdm();
    uint64_t first_page = addr & ~0xFFFUL;
    uint64_t last_page  = (end - 1) & ~0xFFFUL;

    for (uint64_t page = first_page; page <= last_page; page += PAGE_SIZE) {
        uint64_t phys = vmm_get_phys(cr3, page);
        if (phys == 0)
            return false;
        /* Check the page table entry for writable flag */
        uint64_t *pml4 = (uint64_t *)(hd + cr3);
        uint64_t pml4_idx = (page >> 39) & 0x1FF;
        uint64_t pdpt_idx = (page >> 30) & 0x1FF;
        uint64_t pd_idx   = (page >> 21) & 0x1FF;
        uint64_t pt_idx   = (page >> 12) & 0x1FF;

        if (!(pml4[pml4_idx] & PAGE_PRESENT)) return false;
        uint64_t *pdpt = (uint64_t *)(hd + (pml4[pml4_idx] & ~0xFFFUL));
        if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return false;
        uint64_t *pd = (uint64_t *)(hd + (pdpt[pdpt_idx] & ~0xFFFUL));
        if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
        uint64_t *pt = (uint64_t *)(hd + (pd[pd_idx] & ~0xFFFUL));
        if (!(pt[pt_idx] & PAGE_PRESENT)) return false;
        if (!(pt[pt_idx] & PAGE_WRITABLE)) return false;
    }

    return true;
}

bool validate_user_string(const char *str, size_t maxlen) {
    if (str == NULL) return false;

    uint64_t addr = (uint64_t)str;
    if (addr >= USER_ADDR_MAX) return false;

    uint64_t cr3 = vmm_current_pml4();
    size_t checked = 0;

    while (checked < maxlen) {
        uint64_t page = (addr + checked) & ~0xFFFUL;
        if (page >= USER_ADDR_MAX) return false;
        if (vmm_get_phys(cr3, page) == 0) return false;

        uint64_t page_end = page + PAGE_SIZE;
        uint64_t check_end = addr + maxlen;
        if (check_end > page_end) check_end = page_end;

        while ((addr + checked) < check_end && checked < maxlen) {
            char c = *(const char *)(addr + checked);
            if (c == '\0') return true;
            checked++;
        }
    }

    return false; /* no null terminator within maxlen */
}

int copy_from_user(void *dst, const void *src, size_t size) {
    if (!validate_user_ptr(src, size)) return -1;
    memcpy(dst, src, size);
    return 0;
}

int copy_to_user(void *dst, const void *src, size_t size) {
    if (!validate_user_ptr_writable(dst, size)) return -1;
    memcpy(dst, src, size);
    return 0;
}
