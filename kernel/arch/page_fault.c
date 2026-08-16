/*
 * arch/page_fault.c - page fault handler (ISR 14).
 *
 * Kernel faults → panic with diagnostic info.
 * User faults   → terminate the offending process via task_exit().
 */

#include <stdint.h>
#include "drivers/serial.h"
#include "process/scheduler.h"
#include "mm/vmm.h"

/* Page fault error code bits */
#define PF_PRESENT  (1u << 0)
#define PF_WRITE    (1u << 1)
#define PF_USER     (1u << 2)
#define PF_RSVD     (1u << 3)
#define PF_INSTR    (1u << 4)

static void panic_pf(uint64_t rip, uint64_t cr2, uint64_t error_code)
{
    serial_puts("\n\033[91m*** KERNEL PAGE FAULT ***\033[0m\n");
    serial_puts("  RIP = ");
    serial_hex(rip);
    serial_puts("\n  CR2 = ");
    serial_hex(cr2);
    serial_puts("\n  ERR = ");
    serial_hex(error_code);
    serial_puts("\n  ");
    if (error_code & PF_PRESENT) serial_puts("present ");
    if (error_code & PF_WRITE)   serial_puts("write ");
    if (error_code & PF_USER)    serial_puts("user ");
    if (error_code & PF_RSVD)    serial_puts("reserved-bit ");
    if (error_code & PF_INSTR)   serial_puts("instruction-fetch ");
    serial_puts("\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
}

void page_fault_handler(uint64_t error_code, uint64_t rip)
{
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    if (error_code & PF_USER) {
        /* Try COW fault handling first: write to a present, read-only page */
        if ((error_code & (PF_PRESENT | PF_WRITE)) == (PF_PRESENT | PF_WRITE)) {
            uint64_t cr2_aligned = cr2 & ~0xFFFUL;
            if (vmm_handle_cow_fault(vmm_current_pml4(), cr2_aligned) == 0) {
                return;  /* COW fault handled successfully */
            }
        }

        /* User-space page fault - terminate the process */
        serial_puts("page_fault: user process killed (pid=");
        serial_hex((uint64_t)task_current_id());
        serial_puts(" rip=");
        serial_hex(rip);
        serial_puts(" cr2=");
        serial_hex(cr2);
        serial_puts(" err=");
        serial_hex(error_code);
        serial_puts(")\n");

        task_exit_code(-SIGSEGV);  /* signal-like exit code */
        /* task_exit_code never returns */
    }

    /* Kernel page fault - panic */
    panic_pf(rip, cr2, error_code);
}
