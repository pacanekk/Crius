#ifndef ELF_EXEC_H
#define ELF_EXEC_H

#include <stdint.h>
#include <stddef.h>

void elf_exec_init(uint64_t hhdm_offset);
uint64_t elf_exec_get_hhdm(void);
uint64_t elf_load_to_task(uint64_t task_cr3, const void *elf_data, size_t size);

#endif
