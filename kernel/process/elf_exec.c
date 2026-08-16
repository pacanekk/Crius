/*
 * ELF executor - loads a userspace ELF binary from a memory buffer
 * into a task's address space and returns the entry point.
 *
 * Used by the exec syscall to load userspace ELF programs.
 */

#include <stdint.h>
#include <string.h>
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "drivers/serial.h"
#include "process/elf_exec.h"
#include "syscall/usercopy.h"

/* ELF64 header structures */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_hdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

#define PT_LOAD 1
#define PF_X    1
#define PF_W    2
#define PF_R    4

static uint64_t hhdm = 0;

void elf_exec_init(uint64_t hhdm_offset) {
    hhdm = hhdm_offset;
}

uint64_t elf_exec_get_hhdm(void) {
    return hhdm;
}

/*
 * Load ELF from buffer into task's address space (task_cr3).
 * Returns entry point, or 0 on failure.
 *
 * For each PT_LOAD segment:
 *   1. Allocate physical pages via PMM
 *   2. Zero them via HHDM mapping
 *   3. Copy file data via HHDM mapping
 *   4. Map them into the task's address space at p_vaddr
 */
uint64_t elf_load_to_task(uint64_t task_cr3, const void *elf_data, size_t size) {
    if (!elf_data || size < sizeof(elf64_hdr_t)) return 0;

    const elf64_hdr_t *ehdr = (const elf64_hdr_t *)elf_data;

    if (ehdr->e_ident[0] != 0x7f ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        serial_puts("elf_exec: not ELF\n");
        return 0;
    }

    if (ehdr->e_machine != 0x3E) {
        serial_puts("elf_exec: not x86_64\n");
        return 0;
    }

    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * sizeof(elf64_phdr_t) > size) {
        serial_puts("elf_exec: program headers out of bounds\n");
        return 0;
    }

    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)
        ((const uint8_t *)elf_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;

        uint64_t vaddr   = phdrs[i].p_vaddr;
        if (vaddr >= USER_ADDR_MAX || vaddr + phdrs[i].p_memsz > USER_ADDR_MAX) {
            serial_puts("elf_exec: segment vaddr out of user range\n");
            return 0;
        }
        uint64_t filesz  = phdrs[i].p_filesz;
        uint64_t memsz   = phdrs[i].p_memsz;
        uint64_t offset  = phdrs[i].p_offset;

        uint64_t page_off    = vaddr & 0xFFF;
        uint64_t first_page  = vaddr & ~0xFFFUL;
        uint64_t total_bytes = memsz + page_off;
        uint64_t page_count  = (total_bytes + 0xFFF) / 0x1000;

        for (uint64_t p = 0; p < page_count; p++) {
            uint64_t vaddr_page = first_page + p * 0x1000;

            /* Check if page is already mapped from a previous PT_LOAD
             * segment (text/data boundary can share a page). */
            uint64_t existing_phys = vmm_get_phys(task_cr3, vaddr_page);
            uint64_t phys;
            uint8_t *kptr;

            if (existing_phys != 0) {
                phys = existing_phys;
                kptr = (uint8_t *)(hhdm + phys);
            } else {
                phys = pmm_alloc_page();
                if (phys == 0) {
                    serial_puts("elf_exec: OOM\n");
                    return 0;
                }
                kptr = (uint8_t *)(hhdm + phys);
                memset(kptr, 0, 0x1000);
            }

            /* Copy file data into this page via HHDM */
            if (filesz > 0) {
                /* Compute the file offset where this page's data starts.
                 * vaddr_page may be before vaddr (first page of segment),
                 * in which case data starts at page_off within the page. */
                uint64_t seg_page_off;  /* offset within page where segment data starts */
                uint64_t file_off;      /* offset within file data for this page */
                if (vaddr_page >= vaddr) {
                    seg_page_off = 0;
                    file_off = vaddr_page - vaddr;
                } else {
                    seg_page_off = page_off;
                    file_off = 0;
                }
                if (file_off < filesz) {
                    uint64_t bytes_in_page = filesz - file_off;
                    if (bytes_in_page > 0x1000 - seg_page_off)
                        bytes_in_page = 0x1000 - seg_page_off;
                    memcpy(kptr + seg_page_off,
                           (const uint8_t *)elf_data + offset + file_off,
                           bytes_in_page);
                }
            }

            if (existing_phys == 0) {
                uint64_t seg_flags = PAGE_PRESENT | PAGE_USER;
                if (phdrs[i].p_flags & PF_W)
                    seg_flags |= PAGE_WRITABLE;
                if (!(phdrs[i].p_flags & PF_X))
                    seg_flags |= PAGE_NX;

                if (vmm_map_page(task_cr3, vaddr_page, phys,
                             seg_flags) < 0) {
                    pmm_free_page(phys);
                    serial_puts("elf_exec: map_page OOM\n");
                    return 0;
                }
            } else if (phdrs[i].p_flags & PF_W) {
                /* Page already mapped (from text segment) but this
                 * data segment needs write access. Walk the page
                 * table and OR in PAGE_WRITABLE on the PTE. */
                uint64_t *pml4 = (uint64_t *)(hhdm + task_cr3);
                uint64_t *pdpt = (uint64_t *)(hhdm +
                    (pml4[(vaddr_page >> 39) & 0x1FF] & ~0xFFFUL));
                uint64_t *pd = (uint64_t *)(hhdm +
                    (pdpt[(vaddr_page >> 30) & 0x1FF] & ~0xFFFUL));
                uint64_t *pt = (uint64_t *)(hhdm +
                    (pd[(vaddr_page >> 21) & 0x1FF] & ~0xFFFUL));
                pt[(vaddr_page >> 12) & 0x1FF] |= PAGE_WRITABLE;
            }
        }
    }

    return ehdr->e_entry;
}
