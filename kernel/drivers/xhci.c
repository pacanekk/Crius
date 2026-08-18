#include <stdint.h>
#include <string.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

#define XHCI_CLASS      0x0C
#define XHCI_SUBCLASS   0x03
#define XHCI_PROG_IF    0x30

static uint64_t cmd_phys;
static volatile uint32_t *cmd_ring;
static uint64_t event_phys;
static volatile uint32_t *event_ring;

static void xhci_reset(volatile uint8_t *cap, uint8_t caplen);
static void xhci_setup_and_run(volatile uint8_t *cap, uint8_t caplen);
static void xhci_ports_init(volatile uint8_t *cap, uint8_t caplen, uint32_t hcsparams1);
static void xhci_enable_slot(volatile uint8_t *cap);

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
    xhci_setup_and_run(cap, caplen);
    xhci_ports_init(cap, caplen, hcsparams1);
    xhci_enable_slot(cap);
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

static void xhci_ports_init(volatile uint8_t *cap, uint8_t caplen, uint32_t hcsparams1) {
    uint8_t max_ports = (hcsparams1 >> 24) & 0xFF;
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);

    serial_puts("xhci: max_ports=");
    serial_hex(max_ports); serial_puts("\n");

    for (int port = 0; port < max_ports; port++) {
        volatile uint32_t *portsc = &op[0x100 + port * 4];

        /* włącz zasilanie portu */
        *portsc = (1u << 9);
        for (int i = 0; i < 100000; i++)
            __asm__ volatile ("pause");

        uint32_t sc = *portsc;
        serial_puts("xhci: port ");
        serial_hex(port); serial_puts(" sc=");
        serial_hex(sc); serial_puts("\n");

        if (!(sc & 1u)) continue; /* brak urządzenia */

        serial_puts("xhci: port ");
        serial_hex(port); serial_puts(" device connected\n");

        /* reset portu */
        *portsc = (1u << 9) | (1u << 4);
        for (int i = 0; i < 10000; i++) __asm__ volatile ("pause");
        uint32_t sc2 = *portsc;
        serial_puts("xhci: port ");
        serial_hex(port); serial_puts(" after pr sc=");
        serial_hex(sc2); serial_puts("\n");

        int ok = 0;
        for (int i = 0; i < 10000000; i++) {
            uint32_t s = *portsc;
            if (!(s & (1u << 4)) && (s & (1u << 1))) { ok = 1; break; }
        }
        if (ok) {
            uint32_t s = *portsc;
            *portsc = (1u << 9) | (1u << 21); /* wyczyść PLC, zostaw zasilanie */
            serial_puts("xhci: port ");
            serial_hex(port); serial_puts(" reset done ped=");
            serial_hex((s & (1u << 1)) ? 1 : 0); serial_puts(" pls=");
            serial_hex((s >> 5) & 0xF); serial_puts("\n");
        } else {
            serial_puts("xhci: port ");
            serial_hex(port); serial_puts(" reset timeout\n");
        }
    }
}

static void xhci_setup_and_run(volatile uint8_t *cap, uint8_t caplen) {
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);

    cmd_phys = pmm_alloc_page();
    if (cmd_phys == 0) { serial_puts("xhci: no cmd page\n"); return; }
    cmd_ring = (volatile uint32_t *)(vmm_get_hhdm() + cmd_phys);
    memset((void *)cmd_ring, 0, 4096);

    event_phys = pmm_alloc_page();
    if (event_phys == 0) { serial_puts("xhci: no event page\n"); return; }
    event_ring = (volatile uint32_t *)(vmm_get_hhdm() + event_phys);
    memset((void *)event_ring, 0, 4096);

    uint64_t erst_phys = pmm_alloc_page();
    if (erst_phys == 0) { serial_puts("xhci: no erst page\n"); return; }
    volatile uint64_t *erst = (volatile uint64_t *)(vmm_get_hhdm() + erst_phys);
    memset((void *)erst, 0, 4096);

    uint64_t dcbaap_phys = pmm_alloc_page();
    if (dcbaap_phys == 0) { serial_puts("xhci: no dcbaap page\n"); return; }
    uint64_t *dcbaap = (uint64_t *)(vmm_get_hhdm() + dcbaap_phys);
    memset((void *)dcbaap, 0, 4096);

    erst[0] = event_phys;
    erst[1] = 256;

    uint32_t rts_off = *(volatile uint32_t *)(cap + 0x18);
    volatile uint8_t *rt = (volatile uint8_t *)cap + (rts_off & ~0x1F);
    volatile uint32_t *erstsz = (volatile uint32_t *)(rt + 0x28);
    volatile uint64_t *erstba = (volatile uint64_t *)(rt + 0x30);
    volatile uint64_t *erdp = (volatile uint64_t *)(rt + 0x38);
    *erstsz = 1;
    *erstba = erst_phys;
    *erdp = event_phys;

    op[12] = (uint32_t)dcbaap_phys;
    op[13] = (uint32_t)(dcbaap_phys >> 32);
    op[6] = (uint32_t)(cmd_phys | 1);
    op[7] = (uint32_t)(cmd_phys >> 32);
    op[14] = 1;

    op[0] = 1; /* RS */
    int ok = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(op[1] & 1u)) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout HCHalted=0\n"); return; }
    serial_puts("xhci: running\n");
}

static void xhci_enable_slot(volatile uint8_t *cap) {
    if (!cmd_ring) { serial_puts("xhci: cmd ring not set\n"); return; }

    cmd_ring[0] = 0;
    cmd_ring[1] = 0;
    cmd_ring[2] = 0;
    cmd_ring[3] = (9u << 10) | 1u;

    cmd_ring[4] = (uint32_t)cmd_phys;
    cmd_ring[5] = (uint32_t)(cmd_phys >> 32);
    cmd_ring[6] = 0;
    cmd_ring[7] = (6u << 10) | (1u << 1) | 1u;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[0] = 0; /* DB_VALUE_HOST */
    (void)db[0];

    int e = 0;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 33) { ok = 1; break; }
        if (type != 0 && e < 255) e++;
    }
    if (!ok) { serial_puts("xhci: enable slot no event\n"); return; }

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev3 = event_ring[e * 4 + 3];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint8_t slot_id = (uint8_t)(ev3 >> 24);
    serial_puts("xhci: enable slot cc=");
    serial_hex(cc); serial_puts(" slot=");
    serial_hex(slot_id); serial_puts("\n");
}
