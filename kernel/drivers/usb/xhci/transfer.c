#include <stdint.h>
#include <string.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "xhci_internal.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

uint64_t ep0_tr_phys;
volatile uint32_t *ep0_tr;
uint32_t ep0_cycle;
int ep0_enq = 0;
uint8_t xhci_control_in(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, volatile uint32_t *data, uint64_t data_phys) {
    (void)data;
    if (!ep0_tr) { serial_puts("xhci: no ep0 tr\n"); return 0; }

    uint32_t cyc = ep0_cycle;
    int enq = ep0_enq;
    uint64_t next = ep0_tr_phys + (uint64_t)(enq + 16) * 4;

    ep0_tr[enq + 0] = (uint32_t)bmRequestType | ((uint32_t)bRequest << 8) | (((uint32_t)wValue & 0xFFu) << 16) | ((((uint32_t)wValue >> 8) & 0xFFu) << 24);
    ep0_tr[enq + 1] = ((uint32_t)wIndex & 0xFFu) | ((((uint32_t)wIndex >> 8) & 0xFFu) << 8) | (((uint32_t)wLength & 0xFFu) << 16) | ((((uint32_t)wLength >> 8) & 0xFFu) << 24);
    ep0_tr[enq + 2] = 8;
    ep0_tr[enq + 3] = (2u << 10) | (1u << 6) | (1u << 4) | (3u << 16) | cyc;

    ep0_tr[enq + 4] = (uint32_t)data_phys;
    ep0_tr[enq + 5] = (uint32_t)(data_phys >> 32);
    ep0_tr[enq + 6] = (uint32_t)wLength;
    ep0_tr[enq + 7] = (3u << 10) | (1u << 4) | (1u << 16) | cyc;

    ep0_tr[enq + 8] = 0;
    ep0_tr[enq + 9] = 0;
    ep0_tr[enq + 10] = 0;
    ep0_tr[enq + 11] = (4u << 10) | (1u << 5) | cyc;

    ep0_tr[enq + 12] = (uint32_t)next;
    ep0_tr[enq + 13] = (uint32_t)(next >> 32);
    ep0_tr[enq + 14] = 0;
    ep0_tr[enq + 15] = (8u << 10) | cyc;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[xhci_slot_id] = 1; /* DB_VALUE for EP0 */
    (void)db[xhci_slot_id];

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 32) {
            uint32_t ev2 = event_ring[e * 4 + 2];
            uint8_t cc0 = (uint8_t)(ev2 >> 24);
            if (cc0 == 1 || cc0 == 13) { ok = 1; break; }
            if (e < 255) e++;
            continue;
        }
        if (type != 0 && e < 255) e++;
    }
    if (!ok) { serial_puts("xhci: control in no transfer event\n"); return 0; }
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint8_t cc = (uint8_t)(ev2 >> 24);

    if (cc != 1 && cc != 13) {
        serial_puts("xhci: control in cc=");
        serial_hex(cc); serial_puts(" cyc=");
        serial_hex(cyc); serial_puts(" ep0_cycle=");
        serial_hex(ep0_cycle); serial_puts(" evptr=");
        serial_hex(event_ring[e * 4 + 1]); serial_puts(" ");
        serial_hex(event_ring[e * 4 + 0]); serial_puts("\n");
    }

    if (cc == 1 || cc == 13) {
        ep0_enq += 16;
    }
    return cc;
}

uint8_t xhci_control_out(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex) {
    (void)cap;
    if (!ep0_tr) { serial_puts("xhci: no ep0 tr\n"); return 0; }

    uint32_t cyc = ep0_cycle;
    int enq = ep0_enq;
    uint64_t next = ep0_tr_phys + (uint64_t)(enq + 16) * 4;

    ep0_tr[enq + 0] = (uint32_t)bmRequestType | ((uint32_t)bRequest << 8) | (((uint32_t)wValue & 0xFFu) << 16) | ((((uint32_t)wValue >> 8) & 0xFFu) << 24);
    ep0_tr[enq + 1] = ((uint32_t)wIndex & 0xFFu) | ((((uint32_t)wIndex >> 8) & 0xFFu) << 8);
    ep0_tr[enq + 2] = 8;
    ep0_tr[enq + 3] = (2u << 10) | (1u << 6) | (1u << 4) | (1u << 16) | cyc;

    ep0_tr[enq + 4] = 0;
    ep0_tr[enq + 5] = 0;
    ep0_tr[enq + 6] = 0;
    ep0_tr[enq + 7] = (3u << 10) | (1u << 4) | cyc;

    ep0_tr[enq + 8] = 0;
    ep0_tr[enq + 9] = 0;
    ep0_tr[enq + 10] = 0;
    ep0_tr[enq + 11] = (4u << 10) | (1u << 5) | (1u << 16) | cyc;

    ep0_tr[enq + 12] = (uint32_t)next;
    ep0_tr[enq + 13] = (uint32_t)(next >> 32);
    ep0_tr[enq + 14] = 0;
    ep0_tr[enq + 15] = (8u << 10) | cyc;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[xhci_slot_id] = 1;
    (void)db[xhci_slot_id];

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 32) {
            uint32_t ev2 = event_ring[e * 4 + 2];
            uint8_t cc0 = (uint8_t)(ev2 >> 24);
            if (cc0 == 1 || cc0 == 13) { ok = 1; break; }
            if (e < 255) e++;
            continue;
        }
        if (type != 0 && e < 255) e++;
    }
    if (!ok) {
        serial_puts("xhci: control out no event; last type=");
        serial_hex((uint8_t)((event_ring[e * 4 + 3] >> 10) & 0x3F));
        serial_puts(" cc=");
        serial_hex((uint8_t)(event_ring[e * 4 + 2] >> 24));
        serial_puts("\n");
        return 0;
    }
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    if (cc == 1 || cc == 13) {
        ep0_enq += 16;
    }
    return cc;
}

uint8_t xhci_prep_ep0(volatile uint8_t *cap) {
    uint64_t deq = ep0_tr_phys + (uint64_t)ep0_enq * 4;
    uint64_t deq_val = deq | ep0_cycle;
    uint8_t scc;

    scc = xhci_send_command(cap, 0, 0, 0,
        (15u << 10) | (1u << 16) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);

    scc = xhci_send_command(cap, (uint32_t)deq_val, (uint32_t)(deq_val >> 32), 0,
        (16u << 10) | (1u << 16) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);
    return scc;
}

