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
int xhci_ctrl_id = 0;
int cmd_enq = 0; /* enqueue index in TRBs (0..255), points to next free TRB */

void xhci_log_event(int e, const char *ctx) {
    if (!event_ring) return;
    uint32_t dw0 = event_ring[e * 4 + 0];
    uint32_t dw1 = event_ring[e * 4 + 1];
    uint32_t dw2 = event_ring[e * 4 + 2];
    uint32_t dw3 = event_ring[e * 4 + 3];
    uint8_t type = (uint8_t)((dw3 >> 10) & 0x3F);
    uint8_t cyc = (uint8_t)(dw3 & 1u);
    uint8_t cc = (uint8_t)(dw2 >> 24);
    uint8_t slot = (uint8_t)(dw3 >> 24);
    uint8_t ep = (uint8_t)((dw3 >> 16) & 0x1F);
    uint64_t trb_ptr = ((uint64_t)dw1 << 32) | dw0;
    uint32_t xfer_len = dw2 & 0xFFFFFFu;
    const char *tname = "?";
    switch (type) {
        case 32: tname = "XFER"; break;
        case 33: tname = "CCE"; break;
        case 34: tname = "PSC"; break;
        case 37: tname = "HC"; break;
        default: tname = "OTH"; break;
    }
    serial_puts("[EVT c"); serial_hex(xhci_ctrl_id);
    serial_puts(" i="); serial_hex(e);
    serial_puts(" cy="); serial_hex(cyc);
    serial_puts(" "); serial_puts(tname);
    serial_puts(" cc="); serial_hex(cc);
    serial_puts(" sl="); serial_hex(slot);
    serial_puts(" ep="); serial_hex(ep);
    serial_puts(" trb="); serial_hex(trb_ptr);
    serial_puts(" len="); serial_hex(xfer_len);
    serial_puts(" "); serial_puts(ctx); serial_puts("]\n");
}
void xhci_advance_event(int e) {
    if (!xhci_erdp) return;
    xhci_event_idx = e + 1;
    if (xhci_event_idx >= 256) {
        xhci_event_idx = 0;
        xhci_event_cycle ^= 1u;
    }
    /* Write ERDP with bits 2:0 cleared (EHB/DESI) */
    uint64_t new_erdp = (event_phys + (uint64_t)xhci_event_idx * 16) & ~0x7ULL;
    *xhci_erdp = new_erdp;
}
uint32_t cmd_cycle = 1;

void xhci_drain_events(void) {
    if (!event_ring || !xhci_erdp) return;
    serial_puts("[DRAIN c"); serial_hex(xhci_ctrl_id);
    serial_puts(" start idx="); serial_hex(xhci_event_idx);
    serial_puts(" cyc="); serial_hex(xhci_event_cycle);
    serial_puts("]\n");
    for (int i = 0; i < 256; i++) {
        int e = xhci_event_idx;
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) break;
        if ((ev3 & 1u) != xhci_event_cycle) break;
        xhci_log_event(e, "drain");
        xhci_advance_event(e);
    }
    serial_puts("[DRAIN c"); serial_hex(xhci_ctrl_id);
    serial_puts(" end idx="); serial_hex(xhci_event_idx);
    serial_puts(" cyc="); serial_hex(xhci_event_cycle);
    serial_puts("]\n");
}
uint8_t xhci_send_command(volatile uint8_t *cap, uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3) {
    if (!cmd_ring) { serial_puts("xhci: cmd ring not set\n"); return 0; }

    uint8_t cmd_type = (uint8_t)((word3 >> 10) & 0x3F);
    uint32_t cyc = word3 & 1u;
    xhci_fb_last_cmd_type = cmd_type;
    xhci_fb_cmd_enq = (uint8_t)cmd_enq;
    xhci_fb_cmd_cyc = (uint8_t)cyc;
    serial_puts("[CMD c"); serial_hex(xhci_ctrl_id);
    serial_puts(" type="); serial_hex(cmd_type);
    serial_puts(" enq="); serial_hex(cmd_enq);
    serial_puts(" w0="); serial_hex(word0);
    serial_puts(" w1="); serial_hex(word1);
    serial_puts(" w2="); serial_hex(word2);
    serial_puts(" w3="); serial_hex(word3);
    serial_puts(" cyc="); serial_hex(cyc);
    serial_puts("]\n");

    /* Write TRB at current enqueue position.
     * Write fields 0-2 first, then barrier, then field 3 with cycle bit. */
    int off = cmd_enq * 4;
    cmd_ring[off + 0] = word0;
    cmd_ring[off + 1] = word1;
    cmd_ring[off + 2] = word2;
    asm volatile("" ::: "memory"); /* wmb: ensure fields 0-2 visible before cycle bit */
    cmd_ring[off + 3] = word3;

    /* Advance enqueue. If next TRB is the Link TRB (slot 255), toggle its
     * cycle bit and wrap around: XOR the Link TRB cycle bit, then toggle
     * cycle_state. */
    cmd_enq++;
    if (cmd_enq >= 255) {
        /* Toggle Link TRB cycle bit and producer cycle state. */
        cmd_ring[1023] ^= 1u;
        cmd_cycle ^= 1u;
        cmd_enq = 0;
    }

    asm volatile("mfence" ::: "memory");
    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[0] = 0; /* Ring command doorbell */
    (void)db[0]; /* Flush posted write */

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) continue;
        if ((ev3 & 1u) != xhci_event_cycle) continue;
        if (type == 33) { ok = 1; break; }
        xhci_log_event(e, "skip-cmd");
        xhci_advance_event(e);
        e = xhci_event_idx;
    }
    if (!ok) { serial_puts("xhci: command no cce\n"); return 0xFF; }
    xhci_log_event(e, "match-cmd");
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev0 = event_ring[e * 4 + 0];
    uint32_t ev1 = event_ring[e * 4 + 1];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    xhci_fb_last_cmd_cc = cc;
    uint64_t cmd_trb_ptr = ((uint64_t)ev1 << 32) | ev0;
    serial_puts("[CCE c"); serial_hex(xhci_ctrl_id);
    serial_puts(" cc="); serial_hex(cc);
    serial_puts(" cmd_trb="); serial_hex(cmd_trb_ptr);
    serial_puts(" exp_trb="); serial_hex(cmd_phys);
    serial_puts("]\n");
    if (xhci_iman) *xhci_iman = (1u << 1) | (1u << 0); /* clear IP (W1C) + IE */
    return cc;
}

