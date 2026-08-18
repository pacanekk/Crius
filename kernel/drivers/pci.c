#include <stdint.h>
#include "arch/io.h"
#include "drivers/serial.h"
#include "drivers/pci.h"

struct pci_device pci_devices[PCI_MAX_DEVICES];
int pci_device_count = 0;
static uint8_t bus_visited[256];

static uint32_t make_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return 0x80000000UL
        | ((uint32_t)bus << 16)
        | ((uint32_t)(dev & 0x1F) << 11)
        | ((uint32_t)(func & 0x07) << 8)
        | (offset & 0xFC);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, make_addr(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & ~3);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_read_config8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, dev, func, offset & ~3);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    outl(PCI_CONFIG_ADDR, make_addr(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, val);
}

int pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor && pci_devices[i].device_id == device)
            return i;
    }
    return -1;
}

int pci_find_class(uint8_t class, uint8_t subclass) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class && pci_devices[i].subclass == subclass)
            return i;
    }
    return -1;
}

static struct pci_device *pci_add_device(uint8_t bus, uint8_t dev, uint8_t func) {
    if (pci_device_count >= PCI_MAX_DEVICES) return NULL;
    struct pci_device *d = &pci_devices[pci_device_count++];
    d->bus = bus;
    d->dev = dev;
    d->func = func;
    d->vendor_id = pci_read_config16(bus, dev, func, 0x00);
    d->device_id = pci_read_config16(bus, dev, func, 0x02);

    uint32_t class = pci_read_config32(bus, dev, func, 0x08);
    d->revision = (uint8_t)(class & 0xFF);
    d->prog_if = (uint8_t)((class >> 8) & 0xFF);
    d->subclass = (uint8_t)((class >> 16) & 0xFF);
    d->class_code = (uint8_t)((class >> 24) & 0xFF);

    d->header_type = pci_read_config8(bus, dev, func, 0x0E) & 0x7F;
    d->interrupt_line = pci_read_config8(bus, dev, func, 0x3C);

    for (int b = 0; b < 6; b++) {
        d->bars[b] = pci_read_config32(bus, dev, func, 0x10 + b * 4);
    }

    serial_puts("pci ");
    serial_hex(bus); serial_puts(":");
    serial_hex(dev); serial_puts(":");
    serial_hex(func);
    serial_puts(" ven="); serial_hex(d->vendor_id);
    serial_puts(" dev="); serial_hex(d->device_id);
    serial_puts(" class="); serial_hex(d->class_code); serial_puts(":");
    serial_hex(d->subclass); serial_puts(":");
    serial_hex(d->prog_if);
    serial_puts("\n");
    return d;
}

static void pci_scan_bus(uint8_t bus) {
    if (bus >= 256 || bus_visited[bus] || pci_device_count >= PCI_MAX_DEVICES)
        return;
    bus_visited[bus] = 1;

    for (uint8_t dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_read_config16(bus, dev, 0, 0x00);
        if (vendor == 0xFFFF) continue;

        uint8_t header = pci_read_config8(bus, dev, 0, 0x0E);
        uint8_t multifunction = header & 0x80;
        uint8_t functions = multifunction ? 8 : 1;

        for (uint8_t func = 0; func < functions; func++) {
            if (func != 0) {
                vendor = pci_read_config16(bus, dev, func, 0x00);
                if (vendor == 0xFFFF) continue;
            }

            struct pci_device *d = pci_add_device(bus, dev, func);
            if (d && d->header_type == 1) { /* PCI-to-PCI bridge */
                uint8_t secondary = pci_read_config8(bus, dev, func, 0x19);
                uint8_t subordinate = pci_read_config8(bus, dev, func, 0x1A);
                if (secondary != bus && secondary != 0 && subordinate >= secondary && subordinate < 255) {
                    for (uint16_t b = secondary; b <= subordinate && b < 256; b++)
                        pci_scan_bus((uint8_t)b);
                }
            }
        }
    }
}

void pci_init(void) {
    pci_device_count = 0;
    for (int i = 0; i < 256; i++)
        bus_visited[i] = 0;
    pci_scan_bus(0);
    serial_puts("pci: ");
    serial_hex((uint64_t)pci_device_count);
    serial_puts(" devices\n");
}
