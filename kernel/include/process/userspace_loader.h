#ifndef USERSPACE_LOADER_H
#define USERSPACE_LOADER_H

#include <stdint.h>
#include "limine.h"

uint64_t userspace_load(struct limine_module_response *resp, uint64_t hhdm);
uint64_t userspace_get_base(void);
uint64_t userspace_get_entry(void);
uint64_t userspace_get_size(void);
void userspace_map_to_task(uint64_t task_cr3);

#endif
