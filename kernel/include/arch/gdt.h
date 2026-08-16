#ifndef GDT_H
#define GDT_H

#include <stdint.h>

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

void gdt_init(void);
void gdt_load(void);
void tss_load(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
