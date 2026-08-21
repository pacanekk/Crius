#ifndef BOOT_REQUESTS_H
#define BOOT_REQUESTS_H

#include "limine.h"

struct limine_framebuffer_response *boot_get_framebuffer_response(void);
struct limine_hhdm_response *boot_get_hhdm_response(void);
struct limine_executable_address_response *boot_get_exec_addr_response(void);
struct limine_memmap_response *boot_get_memmap_response(void);
struct limine_module_response *boot_get_module_response(void);
struct limine_rsdp_response *boot_get_rsdp_response(void);
const volatile uint64_t *boot_get_base_revision(void);

void boot_early_init(void);
void hcf(void);

#endif
