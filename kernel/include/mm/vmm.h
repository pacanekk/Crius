#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE       4096
#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITABLE   (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_WRITE_THRU (1 << 3)
#define PAGE_NO_CACHE   (1 << 4)
#define PAGE_GLOBAL     (1 << 8)
#define PAGE_COW        (1 << 9)   /* software bit: mark COW pages */
#define PAGE_NX         (1UL << 63)

#define KERNEL_VMA      0xFFFFFFFF80000000UL

uint64_t vmm_create_pml4(void);
int vmm_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
int vmm_map_range(uint64_t pml4_phys, uint64_t virt, uint64_t phys, size_t count, uint64_t flags);
uint64_t vmm_get_phys(uint64_t pml4_phys, uint64_t virt);

void vmm_init(uint64_t hhdm_offset);
uint64_t vmm_get_hhdm(void);

uint64_t vmm_current_pml4(void);

void vmm_switch_pml4(uint64_t pml4_phys);

void vmm_map_kernel(uint64_t pml4_phys);

int vmm_copy_userspace(uint64_t dst_pml4, uint64_t src_pml4);
void vmm_free_user_pages(uint64_t pml4_phys);
int vmm_handle_cow_fault(uint64_t pml4_phys, uint64_t virt);

#endif
