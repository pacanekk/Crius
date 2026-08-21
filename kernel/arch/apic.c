#include <stdint.h>
#include "arch/apic.h"
#include "arch/io.h"

#define LAPIC_PHYS  0xFEE00000UL
#define IOAPIC_PHYS 0xFEC00000UL

#define MMIO_VIRT_BASE 0xFFFF900000000000UL

static uint64_t lapic_base;
static uint64_t ioapic_base;

#define LAPIC_REG(reg) (*(volatile uint32_t *)(lapic_base + (reg)))
#define LAPIC_EOI        0xB0
#define LAPIC_SVR        0xF0
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_LVT_LINT0  0x350
#define LAPIC_LVT_LINT1  0x360
#define LAPIC_LVT_ERROR  0x370
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

static inline void ioapic_write(uint8_t reg, uint32_t val) {
    *(volatile uint32_t *)(ioapic_base + 0x00) = reg;
    *(volatile uint32_t *)(ioapic_base + 0x10) = val;
}

static inline uint32_t ioapic_read(uint8_t reg) {
    *(volatile uint32_t *)(ioapic_base + 0x00) = reg;
    return *(volatile uint32_t *)(ioapic_base + 0x10);
}

void ioapic_set_redirect(uint8_t irq, uint8_t vector) {
    uint8_t entry = irq * 2 + 16;
    uint32_t high = ioapic_read(entry + 1);
    uint32_t low  = ioapic_read(entry);
    low = (low & ~0xFFu) | (uint32_t)vector;    /* set vector */
    low &= ~(1u << 16);                         /* unmask */
    ioapic_write(entry + 1, high);
    ioapic_write(entry, low);
}

static uint64_t hhdm_offset;
static uint64_t kern_phys_base;
static uint64_t kern_virt_base;

static uint64_t pt_storage[4 * 512] __attribute__((aligned(4096)));
static int pt_next = 0;

static uint64_t *alloc_pt(void) {
    uint64_t *pt = &pt_storage[pt_next * 512];
    pt_next++;
    for (int i = 0; i < 512; i++) pt[i] = 0;
    return pt;
}

static uint64_t virt_to_phys(void *ptr) {
    uint64_t v = (uint64_t)ptr;
    if (v >= kern_virt_base) {
        return v - kern_virt_base + kern_phys_base;
    }
    if (v >= hhdm_offset) {
        return v - hhdm_offset;
    }
    return v;
}

static uint64_t map_mmio(uint64_t phys) {
    uint64_t virt = MMIO_VIRT_BASE + phys;
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = (uint64_t *)(hhdm_offset + (cr3 & ~0xFFF));

    if (!(pml4[pml4_idx] & 1)) {
        uint64_t *new_pdpt = alloc_pt();
        pml4[pml4_idx] = virt_to_phys(new_pdpt) | 0x3;
    }

    uint64_t pdpt_phys = pml4[pml4_idx] & ~0xFFF;
    uint64_t *pdpt = (uint64_t *)(hhdm_offset + pdpt_phys);

    if (!(pdpt[pdpt_idx] & 1)) {
        uint64_t *new_pd = alloc_pt();
        pdpt[pdpt_idx] = virt_to_phys(new_pd) | 0x3;
    }

    uint64_t pd_phys = pdpt[pdpt_idx] & ~0xFFF;
    uint64_t *pd = (uint64_t *)(hhdm_offset + pd_phys);

    if (!(pd[pd_idx] & 1)) {
        uint64_t *new_pt = alloc_pt();
        pd[pd_idx] = virt_to_phys(new_pt) | 0x3;
    }

    uint64_t pt_phys = pd[pd_idx] & ~0xFFF;
    uint64_t *pt = (uint64_t *)(hhdm_offset + pt_phys);

    pt[pt_idx] = (phys & ~0xFFF) | 0x3;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt));

    return virt;
}

void apic_init(uint64_t hhdm_offset_param, uint64_t phys_base, uint64_t virt_base) {
    hhdm_offset = hhdm_offset_param;
    kern_phys_base = phys_base;
    kern_virt_base = virt_base;

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    lapic_base  = map_mmio(LAPIC_PHYS);
    ioapic_base = map_mmio(IOAPIC_PHYS);

    uint32_t eax, edx;
    __asm__ volatile ("rdmsr" : "=a"(eax), "=d"(edx) : "c"(0x1B));
    eax |= (1 << 11);
    __asm__ volatile ("wrmsr" : : "a"(eax), "d"(edx), "c"(0x1B));

    LAPIC_REG(LAPIC_LVT_TIMER) = 0x10000;
    LAPIC_REG(LAPIC_LVT_LINT0) = 0x10000;
    LAPIC_REG(LAPIC_LVT_LINT1) = 0x10000;
    LAPIC_REG(LAPIC_LVT_ERROR) = 0x10000;

    LAPIC_REG(LAPIC_SVR) = 0xFF | (1 << 8);

    ioapic_set_redirect(1, 0x21);

    /* Calibrate APIC timer against PIT (1193182 Hz, known frequency).
     * Use PIT channel 2 in one-shot mode to measure APIC ticks in 10ms. */
    uint8_t gate = inb(0x61);
    outb(0x61, gate & ~0x03);          /* disable speaker + channel 2 gate */
    outb(0x43, 0xB0);                  /* channel 2, lobyte/hibyte, mode 0, binary */
    /* PIT count for 10ms: 1193182 / 100 = 11932 */
    outb(0x42, 11932 & 0xFF);
    outb(0x42, (11932 >> 8) & 0xFF);
    outb(0x61, (gate & ~0x02) | 0x01); /* enable channel 2 gate, disable speaker */

    LAPIC_REG(LAPIC_TIMER_DIV) = 0x3;
    LAPIC_REG(LAPIC_TIMER_INIT) = 0xFFFFFFFF;

    /* Wait for PIT channel 2 to count down (bit 5 of port 0x61 goes high) */
    while (!(inb(0x61) & 0x20));

    uint32_t elapsed = 0xFFFFFFFF - LAPIC_REG(LAPIC_TIMER_CUR);
    outb(0x61, gate & ~0x01);          /* disable channel 2 gate, restore */

    /* per_tick = APIC ticks per 1ms */
    uint32_t per_tick = elapsed / 10;

    /* Timer fires every 1ms (periodic mode) */
    LAPIC_REG(LAPIC_LVT_TIMER) = 0x20 | (1 << 17);
    LAPIC_REG(LAPIC_TIMER_DIV) = 0x3;
    LAPIC_REG(LAPIC_TIMER_INIT) = per_tick;
}

void apic_eoi(void) {
    LAPIC_REG(LAPIC_EOI) = 0;
}

