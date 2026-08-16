#include <stdint.h>
#include <string.h>
#include "arch/gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[7];
static struct gdt_ptr gdtr;
static struct tss tss;

static void set_gate(int idx, uint8_t access, uint8_t flags) {
    gdt[idx].limit_low  = 0;
    gdt[idx].base_low   = 0;
    gdt[idx].base_mid   = 0;
    gdt[idx].access     = access;
    gdt[idx].flags      = flags;
    gdt[idx].base_high  = 0;
}

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    /* Index 0: null descriptor */
    set_gate(0, 0, 0);

    /* Index 1: kernel code (selector 0x08) */
    set_gate(1, 0x9A, 0xA0);  /* L=1, present, executable, ring 0 */

    /* Index 2: kernel data (selector 0x10) */
    set_gate(2, 0x92, 0xC0);  /* L=1, present, writable, ring 0 */

    /* Index 3: user data (selector 0x18) */
    set_gate(3, 0xF2, 0xC0);  /* L=1, present, writable, ring 3 */

    /* Index 4: user code (selector 0x20) */
    set_gate(4, 0xFA, 0xA0);  /* L=1, present, executable, ring 3 */

    /* Index 5: TSS (selector 0x28) - 16-byte system descriptor */
    uint64_t tss_addr = (uint64_t)&tss;
    gdt[5].limit_low  = sizeof(struct tss) - 1;
    gdt[5].base_low   = tss_addr & 0xFFFF;
    gdt[5].base_mid   = (tss_addr >> 16) & 0xFF;
    gdt[5].access     = 0x89;  /* present, TSS 64-bit */
    gdt[5].flags      = 0x00;
    gdt[5].base_high  = (tss_addr >> 24) & 0xFF;

    /* The TSS descriptor in long mode is 16 bytes.
     * gdt[5] covers the low 8 bytes; we need to set the high 8 bytes
     * in the next 8 bytes of GDT memory. */
    uint64_t *tss_high = (uint64_t *)&gdt[6];
    *tss_high = (tss_addr >> 32) & 0xFFFFFFFF;

    gdtr.limit = sizeof(gdt) - 1;  /* 7 entries = 56 bytes */
    gdtr.base  = (uint64_t)&gdt;
}

void gdt_load(void) {
    __asm__ volatile ("lgdt %0" : : "m"(gdtr));
    __asm__ volatile (
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "mov $0x08, %%ax\n\t"
        "push %%rax\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "push %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        : : : "rax", "memory"
    );
}

void tss_load(void) {
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
