#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include "limine.h"

void apic_init(uint64_t hhdm_offset_param, uint64_t phys_base, uint64_t virt_base,
               struct limine_rsdp_response *rsdp_resp);
void apic_eoi(void);
void ioapic_set_redirect(uint8_t irq, uint8_t vector);

void apic_mdelay(uint32_t ms);
extern uint32_t apic_per_tick;

#endif
