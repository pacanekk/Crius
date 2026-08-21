#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "arch/io.h"
#include "drivers/serial.h"
#include "drivers/framebuffer.h"
#include "drivers/keyboard.h"
#include "arch/idt.h"
#include "arch/gdt.h"
#include "arch/apic.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "process/scheduler.h"
#include "process/sched_internal.h"
#include "fs/ramfs.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "drivers/ide.h"
#include "process/userspace_loader.h"
#include "process/elf_exec.h"
#include "arch/cpu.h"
#include "boot/boot.h"

int has_smap = 0;
int has_smep = 0;
int has_nx = 0;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void irq32(void); extern void irq33(void); extern void irq34(void);
extern void irq35(void); extern void irq36(void); extern void irq37(void);
extern void irq38(void); extern void irq39(void); extern void irq40(void);
extern void irq41(void); extern void irq42(void); extern void irq43(void);
extern void irq44(void); extern void irq45(void); extern void irq46(void);
extern void irq47(void);
extern void syscall_isr(void);

static void *isr_table[] = {
    isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
    isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

static void *irq_table[] = {
    irq32, irq33, irq34, irq35, irq36, irq37, irq38, irq39,
    irq40, irq41, irq42, irq43, irq44, irq45, irq46, irq47
};

void hcf(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void boot_early_init(void) {
    serial_init();
    serial_puts("Crius kernel successfully booted\n");

    gdt_init();
    gdt_load();
    tss_load();

    /* Check NX support via CPUID.80000001h:EDX[20] */
    {
        uint32_t a, b, c, d;
        __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001));
        if (d & (1 << 20)) has_nx = 1;
    }

    /* Check SMAP/SMEP support via CPUID.07h:EBX[20] (SMAP) and EBX[7] (SMEP) */
    {
        uint32_t a, b, c, d;
        __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7));
        if (b & (1 << 20)) has_smap = 1;
        if (b & (1 << 7))  has_smep = 1;
    }

    /* Enable NX bit (EFER.NXE) for non-executable pages */
    if (has_nx) {
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
        lo |= (1 << 11);  /* NXE */
        __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(0xC0000080));
    }

    /* Enable SMAP and SMEP (CR4 bits 20 and 21) */
    {
        uint64_t cr4;
        __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
        if (has_smap) cr4 |= (1 << 20);
        if (has_smep) cr4 |= (1 << 21);
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
    }

    idt_init();

    for (int i = 0; i < 32; i++) {
        if (i == 8)
            idt_set_gate_ist(i, isr_table[i], 0x8E, 1);
        else
            idt_set_gate(i, isr_table[i], 0x8E);
    }
    for (int i = 0; i < 16; i++) {
        idt_set_gate(32 + i, irq_table[i], 0x8E);
    }

    idt_set_gate(0x80, syscall_isr, 0xEE);

    idt_load();
    apic_init(boot_get_hhdm_response()->offset,
              boot_get_exec_addr_response()->physical_base,
              boot_get_exec_addr_response()->virtual_base,
              boot_get_rsdp_response());

    pmm_init(boot_get_memmap_response());
    kmalloc_init(boot_get_hhdm_response()->offset);
    vmm_init(boot_get_hhdm_response()->offset);
    elf_exec_init(boot_get_hhdm_response()->offset);
    scheduler_init();
    ramfs_init();
    ide_init();
    ide_register_block_devices();

    int has_fb = 0;
    struct limine_framebuffer_response *fb_resp = boot_get_framebuffer_response();
    if (fb_resp != NULL && fb_resp->framebuffer_count > 0) {
        fb_init(fb_resp->framebuffers[0]);
        fb_clear(0x00000000);
        fb_puts("Crius kernel successfully booted\n", 0x00FFFFFF, 0x00000000);
        has_fb = 1;
    }
    fb_set_available(has_fb);

    vfs_init();
}
