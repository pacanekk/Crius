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
uint32_t xhci_last_xfer_len = 0;
uint8_t xhci_control_in(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, volatile uint32_t *data, uint64_t data_phys) {
    (void)data;
    if (!ep0_tr) { serial_puts("xhci: no ep0 tr\n"); return 0; }

    uint32_t cyc = ep0_cycle;
    int enq = ep0_enq;
    serial_puts("[CTRL IN c"); serial_hex(xhci_ctrl_id);
    serial_puts(" rt="); serial_hex(bmRequestType);
    serial_puts(" req="); serial_hex(bRequest);
    serial_puts(" val="); serial_hex(wValue);
    serial_puts(" idx="); serial_hex(wIndex);
    serial_puts(" len="); serial_hex(wLength);
    serial_puts(" slot="); serial_hex(xhci_slot_id);
    serial_puts(" enq="); serial_hex(enq);
    serial_puts(" cyc="); serial_hex(cyc);
    serial_puts(" ep0_phys="); serial_hex(ep0_tr_phys);
    serial_puts("]\n");
    ep0_tr[enq + 0] = (uint32_t)bmRequestType | ((uint32_t)bRequest << 8) | (((uint32_t)wValue & 0xFFu) << 16) | ((((uint32_t)wValue >> 8) & 0xFFu) << 24);
    ep0_tr[enq + 1] = ((uint32_t)wIndex & 0xFFu) | ((((uint32_t)wIndex >> 8) & 0xFFu) << 8) | (((uint32_t)wLength & 0xFFu) << 16) | ((((uint32_t)wLength >> 8) & 0xFFu) << 24);
    ep0_tr[enq + 2] = 8;
    ep0_tr[enq + 3] = (2u << 10) | (1u << 6) | (1u << 16) | cyc; /* Setup: IDT=1, TT=1 (IN data) */

    ep0_tr[enq + 4] = (uint32_t)data_phys;
    ep0_tr[enq + 5] = (uint32_t)(data_phys >> 32);
    ep0_tr[enq + 6] = (uint32_t)wLength;
    ep0_tr[enq + 7] = (3u << 10) | (1u << 16) | cyc;

    ep0_tr[enq + 8] = 0;
    ep0_tr[enq + 9] = 0;
    ep0_tr[enq + 10] = 0;
    ep0_tr[enq + 11] = (4u << 10) | (1u << 5) | (1u << 16) | cyc; /* Status: Direction=1 (OUT) for IN data transfer */

    serial_puts("[TRB SETUP phys="); serial_hex(ep0_tr_phys + (uint64_t)enq * 4);
    serial_puts(" dw0="); serial_hex(ep0_tr[enq + 0]);
    serial_puts(" dw1="); serial_hex(ep0_tr[enq + 1]);
    serial_puts(" dw2="); serial_hex(ep0_tr[enq + 2]);
    serial_puts(" dw3="); serial_hex(ep0_tr[enq + 3]);
    serial_puts("]\n");
    serial_puts("[TRB DATA phys="); serial_hex(ep0_tr_phys + (uint64_t)(enq + 4) * 4);
    serial_puts(" dw0="); serial_hex(ep0_tr[enq + 4]);
    serial_puts(" dw1="); serial_hex(ep0_tr[enq + 5]);
    serial_puts(" dw2="); serial_hex(ep0_tr[enq + 6]);
    serial_puts(" dw3="); serial_hex(ep0_tr[enq + 7]);
    serial_puts("]\n");
    serial_puts("[TRB STATUS phys="); serial_hex(ep0_tr_phys + (uint64_t)(enq + 8) * 4);
    serial_puts(" dw0="); serial_hex(ep0_tr[enq + 8]);
    serial_puts(" dw1="); serial_hex(ep0_tr[enq + 9]);
    serial_puts(" dw2="); serial_hex(ep0_tr[enq + 10]);
    serial_puts(" dw3="); serial_hex(ep0_tr[enq + 11]);
    serial_puts("]\n");
    {
        uint32_t s_dw3 = ep0_tr[enq + 3];
        uint32_t d_dw3 = ep0_tr[enq + 7];
        uint32_t st_dw3 = ep0_tr[enq + 11];
        serial_puts("[DECODE SETUP type="); serial_hex((s_dw3 >> 10) & 0x3F);
        serial_puts(" trt="); serial_hex((s_dw3 >> 16) & 0x3);
        serial_puts(" idt="); serial_hex((s_dw3 >> 6) & 1u);
        serial_puts(" cyc="); serial_hex(s_dw3 & 1u);
        serial_puts("]\n");
        serial_puts("[DECODE DATA type="); serial_hex((d_dw3 >> 10) & 0x3F);
        serial_puts(" dir="); serial_hex((d_dw3 >> 16) & 1u);
        serial_puts(" cyc="); serial_hex(d_dw3 & 1u);
        serial_puts(" len="); serial_hex(ep0_tr[enq + 6]);
        serial_puts("]\n");
        serial_puts("[DECODE STATUS type="); serial_hex((st_dw3 >> 10) & 0x3F);
        serial_puts(" dir="); serial_hex((st_dw3 >> 16) & 1u);
        serial_puts(" ioc="); serial_hex((st_dw3 >> 5) & 1u);
        serial_puts(" cyc="); serial_hex(st_dw3 & 1u);
        serial_puts("]\n");
    }

    asm volatile("mfence" ::: "memory");
    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    serial_puts("[DB c"); serial_hex(xhci_ctrl_id);
    serial_puts(" db="); serial_hex(xhci_slot_id);
    serial_puts(" val=1]\n");
    db[xhci_slot_id] = 1; /* DB_VALUE for EP0 */
    (void)db[xhci_slot_id];

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) continue;
        if ((ev3 & 1u) != xhci_event_cycle) continue;
        if (type == 32) {
            uint8_t ev_slot = (uint8_t)(ev3 >> 24);
            uint8_t ev_ep = (uint8_t)((ev3 >> 16) & 0x1F);
            if (ev_slot == xhci_slot_id && ev_ep == 1) { ok = 1; break; }
        }
        xhci_log_event(e, "skip-xfer-in");
        xhci_advance_event(e);
        e = xhci_event_idx;
    }
    if (!ok) { serial_puts("xhci: control in no transfer event\n"); return 0; }
    xhci_log_event(e, "match-xfer-in");
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev0 = event_ring[e * 4 + 0];
    uint32_t ev1 = event_ring[e * 4 + 1];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint64_t ev_trb_ptr = ((uint64_t)ev1 << 32) | ev0;
    uint32_t xfer_len = ev2 & 0xFFFFFFu;
    xhci_last_xfer_len = xfer_len;
    serial_puts("[XFER IN c"); serial_hex(xhci_ctrl_id);
    serial_puts(" cc="); serial_hex(cc);
    serial_puts(" trb="); serial_hex(ev_trb_ptr);
    serial_puts(" len="); serial_hex(xfer_len);
    serial_puts(" exp_status_trb="); serial_hex(ep0_tr_phys + (uint64_t)(enq + 8) * 4);
    serial_puts("]\n");

    if (cc != 0 && cc != 13) {
        serial_puts("xhci: control in FAIL cc=");
        serial_hex(cc); serial_puts("\n");
    }

    if (cc == 0 || cc == 13) {
        ep0_enq += 12;
        if (ep0_enq >= 1020) {
            ep0_cycle ^= 1u; /* Toggle cycle first */
            ep0_tr[1023] = (6u << 10) | (1u << 1) | ep0_cycle; /* Link TRB with new cycle */
            ep0_enq = 0;
        }
    }
    return cc;
}

uint8_t xhci_control_out(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex) {
    if (!ep0_tr) { serial_puts("xhci: no ep0 tr\n"); return 0; }

    uint32_t cyc = ep0_cycle;
    int enq = ep0_enq;
    serial_puts("[CTRL OUT c"); serial_hex(xhci_ctrl_id);
    serial_puts(" rt="); serial_hex(bmRequestType);
    serial_puts(" req="); serial_hex(bRequest);
    serial_puts(" val="); serial_hex(wValue);
    serial_puts(" idx="); serial_hex(wIndex);
    serial_puts(" slot="); serial_hex(xhci_slot_id);
    serial_puts(" enq="); serial_hex(enq);
    serial_puts(" cyc="); serial_hex(cyc);
    serial_puts("]\n");

    ep0_tr[enq + 0] = (uint32_t)bmRequestType | ((uint32_t)bRequest << 8) | (((uint32_t)wValue & 0xFFu) << 16) | ((((uint32_t)wValue >> 8) & 0xFFu) << 24);
    ep0_tr[enq + 1] = ((uint32_t)wIndex & 0xFFu) | ((((uint32_t)wIndex >> 8) & 0xFFu) << 8);
    ep0_tr[enq + 2] = 8;
    ep0_tr[enq + 3] = (2u << 10) | (1u << 6) | (3u << 16) | cyc; /* Setup: IDT=1, TRT=3 (No Data) */

    ep0_tr[enq + 4] = 0;
    ep0_tr[enq + 5] = 0;
    ep0_tr[enq + 6] = 0;
    ep0_tr[enq + 7] = (4u << 10) | (1u << 5) | cyc; /* Status: Direction=0 (IN) for no-data transfer */

    serial_puts("[TRB SETUP phys="); serial_hex(ep0_tr_phys + (uint64_t)enq * 4);
    serial_puts(" dw0="); serial_hex(ep0_tr[enq + 0]);
    serial_puts(" dw1="); serial_hex(ep0_tr[enq + 1]);
    serial_puts(" dw2="); serial_hex(ep0_tr[enq + 2]);
    serial_puts(" dw3="); serial_hex(ep0_tr[enq + 3]);
    serial_puts("]\n");
    serial_puts("[TRB STATUS phys="); serial_hex(ep0_tr_phys + (uint64_t)(enq + 4) * 4);
    serial_puts(" dw0="); serial_hex(ep0_tr[enq + 4]);
    serial_puts(" dw1="); serial_hex(ep0_tr[enq + 5]);
    serial_puts(" dw2="); serial_hex(ep0_tr[enq + 6]);
    serial_puts(" dw3="); serial_hex(ep0_tr[enq + 7]);
    serial_puts("]\n");
    {
        uint32_t s_dw3 = ep0_tr[enq + 3];
        uint32_t st_dw3 = ep0_tr[enq + 7];
        serial_puts("[DECODE SETUP type="); serial_hex((s_dw3 >> 10) & 0x3F);
        serial_puts(" trt="); serial_hex((s_dw3 >> 16) & 0x3);
        serial_puts(" idt="); serial_hex((s_dw3 >> 6) & 1u);
        serial_puts(" cyc="); serial_hex(s_dw3 & 1u);
        serial_puts("]\n");
        serial_puts("[DECODE STATUS type="); serial_hex((st_dw3 >> 10) & 0x3F);
        serial_puts(" dir="); serial_hex((st_dw3 >> 16) & 1u);
        serial_puts(" ioc="); serial_hex((st_dw3 >> 5) & 1u);
        serial_puts(" cyc="); serial_hex(st_dw3 & 1u);
        serial_puts("]\n");
    }

    asm volatile("mfence" ::: "memory");
    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    serial_puts("[DB c"); serial_hex(xhci_ctrl_id);
    serial_puts(" db="); serial_hex(xhci_slot_id);
    serial_puts(" val=1]\n");
    db[xhci_slot_id] = 1;
    (void)db[xhci_slot_id];

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) continue;
        if ((ev3 & 1u) != xhci_event_cycle) continue;
        if (type == 32) {
            uint8_t ev_slot = (uint8_t)(ev3 >> 24);
            uint8_t ev_ep = (uint8_t)((ev3 >> 16) & 0x1F);
            if (ev_slot == xhci_slot_id && ev_ep == 1) { ok = 1; break; }
        }
        xhci_log_event(e, "skip-xfer-out");
        xhci_advance_event(e);
        e = xhci_event_idx;
    }
    if (!ok) {
        serial_puts("xhci: control out no event\n");
        return 0;
    }
    xhci_log_event(e, "match-xfer-out");
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev0 = event_ring[e * 4 + 0];
    uint32_t ev1 = event_ring[e * 4 + 1];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint64_t ev_trb_ptr = ((uint64_t)ev1 << 32) | ev0;
    uint32_t xfer_len = ev2 & 0xFFFFFFu;
    serial_puts("[XFER OUT c"); serial_hex(xhci_ctrl_id);
    serial_puts(" cc="); serial_hex(cc);
    serial_puts(" trb="); serial_hex(ev_trb_ptr);
    serial_puts(" len="); serial_hex(xfer_len);
    serial_puts(" exp_status_trb="); serial_hex(ep0_tr_phys + (uint64_t)(enq + 4) * 4);
    serial_puts("]\n");
    if (cc == 0 || cc == 13) {
        ep0_enq += 8;
        if (ep0_enq >= 1020) {
            ep0_cycle ^= 1u; /* Toggle cycle first */
            ep0_tr[1023] = (6u << 10) | (1u << 1) | ep0_cycle; /* Link TRB with new cycle */
            ep0_enq = 0;
        }
    }
    return cc;
}

uint8_t xhci_prep_ep0(volatile uint8_t *cap) {
    uint64_t deq = ep0_tr_phys + (uint64_t)ep0_enq * 4;
    uint8_t scc;

    scc = xhci_send_command(cap, 0, 0, 0,
        (15u << 10) | (1u << 16) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);

    /* DCS bit goes in DW2 bit 0, not in the dequeue pointer */
    scc = xhci_send_command(cap, (uint32_t)deq, (uint32_t)(deq >> 32), ep0_cycle & 1u,
        (16u << 10) | (1u << 16) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);
    return scc;
}

