#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

struct pci_device {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint32_t bars[6];
};

#define PCI_MAX_DEVICES 64
extern struct pci_device pci_devices[PCI_MAX_DEVICES];
extern int pci_device_count;

uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t  pci_read_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

void pci_init(void);

int pci_find_device(uint16_t vendor, uint16_t device);
int pci_find_class(uint8_t class, uint8_t subclass);

#endif
