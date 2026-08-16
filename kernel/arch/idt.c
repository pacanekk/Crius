#include <stdint.h>
#include "arch/idt.h"
#include "arch/page_fault.h"
#include "drivers/framebuffer.h"
#include "drivers/serial.h"
#include "process/scheduler.h"

static struct idt_entry idt[256];
static struct idt_ptr idtr;

static const char *exception_names[] = {
    "Division by zero", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 FPU exception", "Alignment check", "Machine check", "SIMD exception",
    "Virtualization exception", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security exception", "Reserved"
};

void idt_set_gate(int vector, void *handler, uint8_t type_attr) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low  = addr & 0xFFFF;
    idt[vector].selector    = 0x08;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
    idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[vector].zero        = 0;
}

void idt_init(void) {
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0);
    }
}

void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

void isr_handler(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs) {
    if (vector == 14) {
        page_fault_handler(error_code, rip);
        return;
    }
    if (vector < 32) {
        /* If the fault came from userspace (RPL=3), kill the process */
        if ((cs & 3) == 3) {
            const char *name = (vector < 32) ? exception_names[vector] : "Unknown";
            serial_puts("exception: user process killed (pid=");
            serial_hex((uint64_t)task_current_id());
            serial_puts(" vec=");
            serial_hex(vector);
            serial_puts(" '");
            serial_puts(name);
            serial_puts("' rip=");
            serial_hex(rip);
            serial_puts(")\n");
            task_exit_code(-SIGSEGV);
        }

        /* Kernel exception - panic */
        const char *name = exception_names[vector];
        serial_puts("\n\033[91m*** KERNEL EXCEPTION ***\033[0m\n");
        serial_puts("  ");
        serial_puts(name);
        serial_puts(" rip=");
        serial_hex(rip);
        serial_puts(" err=");
        serial_hex(error_code);
        serial_puts("\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
}
