#ifndef APIC_H
#define APIC_H

#include <stdint.h>

void apic_init(uint64_t hhdm_offset, uint64_t phys_base, uint64_t virt_base);
void apic_eoi(void);
void ioapic_set_redirect(uint8_t irq, uint8_t vector);

#endif
