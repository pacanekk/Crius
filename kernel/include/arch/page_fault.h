#ifndef PAGE_FAULT_H
#define PAGE_FAULT_H

#include <stdint.h>

void page_fault_handler(uint64_t error_code, uint64_t rip);

#endif
