#include <stdint.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "mm/vmm.h"

#define XHCI_CLASS      0x0C
#define XHCI_SUBCLASS   0x03
#define XHCI_PROG_IF    0x30

static void xhci_reset(volatile uint8_t *cap, uint8_t caplen);

static uint64_t pci_bar_addr(const struct pci_device *d, int bar) {
    if (bar >= 6) return 0;
    uint32_t lo = d->bars[bar];
    if (lo & 1) return 0;             /* I/O BAR, nie pamięci */
    uint32_t type = lo & 0x6;
    uint32_t addr = lo & ~0xF;
    if (type == 0x4 && (bar + 1) < 6) { /* 64-bit */
        uint64_t hi = d->bars[bar + 1];
        return (hi << 32) | addr;
    }
    return addr;
}

void xhci_init(void) {
    struct pci_device *xhci = NULL;
    for (int i = 0; i < pci_device_count; i++) {
        struct pci_device *d = &pci_devices[i];
        if (d->class_code == XHCI_CLASS &&
            d->subclass == XHCI_SUBCLASS &&
            d->prog_if == XHCI_PROG_IF) {
            xhci = d;
            break;
        }
    }
    if (!xhci) {
        serial_puts("xhci: no controller found\n");
        return;
    }

    serial_puts("xhci: found at ");
    serial_hex(xhci->bus); serial_puts(":");
    serial_hex(xhci->dev); serial_puts(":");
    serial_hex(xhci->func); serial_puts("\n");

    uint64_t bar = pci_bar_addr(xhci, 0);
    if (bar == 0) {
        serial_puts("xhci: invalid or I/O BAR\n");
        return;
    }
    serial_puts("xhci: bar0=");
    serial_hex(bar); serial_puts("\n");

    uint64_t vbase = 0xFFFFFFFFA0000000UL;
    if (vmm_map_range(vmm_current_pml4(), vbase, bar & ~0xFFFUL, 16,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_CACHE) < 0) {
        serial_puts("xhci: vmm_map_range failed\n");
        return;
    }
    serial_puts("xhci: mmio mapped at ");
    serial_hex(vbase); serial_puts("\n");

    volatile uint8_t *cap = (volatile uint8_t *)(vbase + (bar & 0xFFFUL));
    uint8_t caplen   = *(volatile uint8_t *)cap;
    uint16_t version = *(volatile uint16_t *)(cap + 0x02);
    uint32_t hcsparams1 = *(volatile uint32_t *)(cap + 0x04);
    uint32_t hcsparams2 = *(volatile uint32_t *)(cap + 0x08);
    uint32_t hccparams1 = *(volatile uint32_t *)(cap + 0x10);

    serial_puts("xhci: caplen=");
    serial_hex(caplen); serial_puts(" version=");
    serial_hex(version); serial_puts("\n");
    serial_puts("xhci: hcsparams1=");
    serial_hex(hcsparams1); serial_puts(" hcsparams2=");
    serial_hex(hcsparams2); serial_puts("\n");
    serial_puts("xhci: hccparams1=");
    serial_hex(hccparams1); serial_puts("\n");

    xhci_reset(cap, caplen);
}

static void xhci_reset(volatile uint8_t *cap, uint8_t caplen) {
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);
    int ok = 0;

    /* czekaj, aż kontroler będzie gotowy (CNR = 0) */
    for (int i = 0; i < 1000000; i++) {
        if (!(op[1] & (1u << 11))) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout CNR=0\n"); return; }

    /* zatrzymaj, jeśli działa */
    if (op[0] & 1u) {
        op[0] &= ~1u;
        ok = 0;
        for (int i = 0; i < 1000000; i++) {
            if (op[1] & 1u) { ok = 1; break; }
        }
        if (!ok) { serial_puts("xhci: timeout HCHalted\n"); return; }
    }

    /* zresetuj */
    op[0] = (1u << 1);
    ok = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(op[0] & (1u << 1))) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout HCRST\n"); return; }

    /* gotowy po resecie */
    ok = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(op[1] & (1u << 11))) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout CNR=0 after reset\n"); return; }

    serial_puts("xhci: reset ok\n");
}
