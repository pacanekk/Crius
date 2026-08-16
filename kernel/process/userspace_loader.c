/*
 * Userspace ELF Loader - loads the first userspace ELF module into memory.
 *
 * Parses the ELF64 file provided by Limine as a module,
 * maps its PT_LOAD segments into kernel-accessible memory,
 * and returns the entry point.
 */

#include <stdint.h>
#include <string.h>
#include "limine.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/kmalloc.h"
#include "drivers/serial.h"
#include "process/userspace_loader.h"
#include "arch/cpu.h"

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

static uint64_t hhdm_offset = 0;

/* Userspace image base address and entry point */
static uint64_t userspace_base = 0;
static uint64_t userspace_entry = 0;
static uint64_t userspace_size = 0;

uint64_t userspace_get_base(void)  { return userspace_base; }
uint64_t userspace_get_entry(void) { return userspace_entry; }
uint64_t userspace_get_size(void)  { return userspace_size; }

uint64_t userspace_load(struct limine_module_response *resp, uint64_t hhdm) {
    hhdm_offset = hhdm;

    if (!resp || resp->module_count == 0) {
        serial_puts("userspace_loader: no modules found\n");
        return 0;
    }

    /* Use the first Limine module as the first userspace ELF.
     * The kernel does not know or care what the module is called -
     * any valid ELF module can serve as PID 1. */
    struct limine_file *mod = resp->modules[0];

    serial_puts("userspace_loader: found module at ");
    serial_hex((uint64_t)mod->address);
    serial_puts(" size=");
    serial_hex(mod->size);
    serial_puts("\n");

    /* Parse ELF header */
    elf64_hdr_t *ehdr = (elf64_hdr_t *)mod->address;

    if (ehdr->e_ident[0] != 0x7f ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        serial_puts("userspace_loader: not an ELF file\n");
        return 0;
    }

    if (ehdr->e_machine != 0x3E) {  /* EM_X86_64 */
        serial_puts("userspace_loader: not x86_64\n");
        return 0;
    }

    userspace_entry = ehdr->e_entry;

    /* Parse program headers */
    elf64_phdr_t *phdrs = (elf64_phdr_t *)((uint8_t *)mod->address + ehdr->e_phoff);

    uint64_t lowest = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t highest = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        if (phdrs[i].p_memsz == 0) continue;
        if (phdrs[i].p_vaddr < lowest) lowest = phdrs[i].p_vaddr;
        uint64_t seg_end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        if (seg_end > highest) highest = seg_end;
    }

    /* Page-align */
    lowest &= ~0xFFFUL;
    highest = (highest + 0xFFF) & ~0xFFFUL;
    userspace_base = lowest;
    userspace_size = highest - lowest;

    serial_puts("userspace_loader: base=");
    serial_hex(userspace_base);
    serial_puts(" size=");
    serial_hex(userspace_size);
    serial_puts(" entry=");
    serial_hex(userspace_entry);
    serial_puts("\n");

    /* Map and load each PT_LOAD segment */
    uint64_t cr3 = vmm_current_pml4();

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        if (phdrs[i].p_memsz == 0) continue;

        uint64_t vaddr = phdrs[i].p_vaddr;
        uint64_t filesz = phdrs[i].p_filesz;
        uint64_t memsz = phdrs[i].p_memsz;
        uint64_t offset = phdrs[i].p_offset;

        uint64_t page_count = (memsz + (vaddr & 0xFFF) + 0xFFF) / 0x1000;
        uint64_t first_page = vaddr & ~0xFFFUL;

        serial_puts("userspace_loader: segment vaddr=");
        serial_hex(vaddr);
        serial_puts(" pages=");
        serial_hex(page_count);
        serial_puts("\n");

        for (uint64_t p = 0; p < page_count; p++) {
            uint64_t phys = pmm_alloc_page();
            if (phys == 0) {
                serial_puts("userspace_loader: out of memory\n");
                return 0;
            }
            /* Zero the page */
            memset((void *)(hhdm_offset + phys), 0, 0x1000);
            /* Map writable during loading so we can copy file data */
            if (vmm_map_page(cr3, first_page + p * 0x1000, phys,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0) {
                pmm_free_page(phys);
                serial_puts("userspace_loader: map_page failed\n");
                return 0;
            }
        }

        /* Copy file data into mapped pages */
        if (filesz > 0) {
            uint8_t *src = (uint8_t *)mod->address + offset;
            uint8_t *dst = (uint8_t *)vaddr;  /* Direct access via kernel mapping */
            if (has_smap) __asm__ volatile ("stac" ::: "memory");
            memcpy(dst, src, filesz);
            if (has_smap) __asm__ volatile ("clac" ::: "memory");
        }
    }

    /* Fix PTE permissions: set correct W/X flags per segment */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type != PT_LOAD) continue;
        if (phdrs[i].p_memsz == 0) continue;

        uint64_t vaddr = phdrs[i].p_vaddr;
        uint64_t memsz = phdrs[i].p_memsz;
        uint64_t page_count = (memsz + (vaddr & 0xFFF) + 0xFFF) / 0x1000;
        uint64_t first_page = vaddr & ~0xFFFUL;

        for (uint64_t p = 0; p < page_count; p++) {
            uint64_t virt = first_page + p * 0x1000;
            uint64_t pml4_idx = (virt >> 39) & 0x1FF;
            uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
            uint64_t pd_idx   = (virt >> 21) & 0x1FF;
            uint64_t pt_idx   = (virt >> 12) & 0x1FF;
            uint64_t *pml4 = (uint64_t *)(hhdm_offset + cr3);
            if (!(pml4[pml4_idx] & PAGE_PRESENT)) continue;
            uint64_t *pdpt = (uint64_t *)(hhdm_offset + (pml4[pml4_idx] & ~0xFFFUL));
            if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) continue;
            uint64_t *pd = (uint64_t *)(hhdm_offset + (pdpt[pdpt_idx] & ~0xFFFUL));
            if (!(pd[pd_idx] & PAGE_PRESENT)) continue;
            uint64_t *pt = (uint64_t *)(hhdm_offset + (pd[pd_idx] & ~0xFFFUL));
            if (!(pt[pt_idx] & PAGE_PRESENT)) continue;

            uint64_t pte = pt[pt_idx];
            if (!(phdrs[i].p_flags & PF_W))
                pte &= ~PAGE_WRITABLE;
            if (!(phdrs[i].p_flags & PF_X))
                pte |= PAGE_NX;
            pt[pt_idx] = pte;
        }
    }

    serial_puts("userspace_loader: first process loaded successfully\n");
    return userspace_entry;
}

/* Map userspace image pages into a task's address space */
void userspace_map_to_task(uint64_t task_cr3) {
    if (userspace_base == 0) return;

    uint64_t cr3 = vmm_current_pml4();
    uint64_t page_count = (userspace_size + 0xFFF) / 0x1000;

    for (uint64_t p = 0; p < page_count; p++) {
        uint64_t virt = userspace_base + p * 0x1000;
        uint64_t phys = vmm_get_phys(cr3, virt);
        if (phys == 0) continue;
        /* Read original flags from source mapping to preserve NX/W permissions */
        uint64_t *pml4 = (uint64_t *)(hhdm_offset + cr3);
        uint64_t pml4_idx = (virt >> 39) & 0x1FF;
        uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
        uint64_t pd_idx   = (virt >> 21) & 0x1FF;
        uint64_t pt_idx   = (virt >> 12) & 0x1FF;
        uint64_t *pdpt = (uint64_t *)(hhdm_offset + (pml4[pml4_idx] & ~0xFFFUL));
        uint64_t *pd   = (uint64_t *)(hhdm_offset + (pdpt[pdpt_idx] & ~0xFFFUL));
        uint64_t *pt   = (uint64_t *)(hhdm_offset + (pd[pd_idx] & ~0xFFFUL));
        uint64_t orig_flags = pt[pt_idx] & (0xFFFUL | PAGE_NX);
        if (vmm_map_page(task_cr3, virt, phys,
                     PAGE_PRESENT | PAGE_USER | orig_flags) < 0)
            continue;
    }
}
