#include <stdint.h>
#include <string.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "xhci_internal.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

uint64_t cmd_phys;
volatile uint32_t *cmd_ring;
uint64_t event_phys;
volatile uint32_t *event_ring;
int xhci_event_idx = 0;
uint32_t xhci_event_cycle = 1;
volatile uint64_t *xhci_erdp;
void xhci_advance_event(int e) {
    if (!xhci_erdp) return;
    xhci_event_idx = e + 1;
    if (xhci_event_idx >= 256) {
        xhci_event_idx = 0;
        xhci_event_cycle ^= 1u;
    }
    *xhci_erdp = (event_phys + (uint64_t)xhci_event_idx * 16); /* DESI=0, no EHB */
}
uint32_t cmd_cycle = 1;

void xhci_drain_events(void) {
    if (!event_ring || !xhci_erdp) return;
    for (int i = 0; i < 256; i++) {
        int e = xhci_event_idx;
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) break;
        if ((ev3 & 1u) != xhci_event_cycle) break;
        xhci_advance_event(e);
    }
}
uint8_t xhci_send_command(volatile uint8_t *cap, uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3) {
    if (!cmd_ring) { serial_puts("xhci: cmd ring not set\n"); return 0; }

    cmd_ring[0] = word0;
    cmd_ring[1] = word1;
    cmd_ring[2] = word2;
    cmd_ring[3] = word3;

    cmd_ring[4] = (uint32_t)cmd_phys;
    cmd_ring[5] = (uint32_t)(cmd_phys >> 32);
    cmd_ring[6] = 0;
    cmd_ring[7] = (6u << 10) | (1u << 1) | (word3 & 1u);

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[0] = 0;
    (void)db[0];
    cmd_cycle ^= 1u;

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) continue;
        if ((ev3 & 1u) != xhci_event_cycle) continue;
        if (type == 33) { ok = 1; break; }
        xhci_advance_event(e);
        e = xhci_event_idx;
    }
    if (!ok) { serial_puts("xhci: command no cce\n"); return 0; }
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    if (xhci_iman) *xhci_iman = (1u << 1) | (1u << 0); /* clear IP (W1C) + IE */
    return cc;
}

