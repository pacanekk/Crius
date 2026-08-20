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
static uint64_t *xhci_dcbaap;
static volatile uint32_t *xhci_dev_ctx;
static int xhci_connected_port = -1;
static uint32_t xhci_portsc = 0;
static uint8_t xhci_slot_id = 0;
static uint32_t xhci_pspd = 0;
static uint32_t xhci_root_port = 0;
static int xhci_event_idx = 0;
static uint32_t xhci_event_cycle = 1;
static volatile uint64_t *xhci_erdp;

static void xhci_advance_event(int e) {
    if (!xhci_erdp) return;
    xhci_event_idx = e + 1;
    if (xhci_event_idx >= 256) {
        xhci_event_idx = 0;
        xhci_event_cycle ^= 1u;
    }
    *xhci_erdp = (event_phys + (uint64_t)xhci_event_idx * 16) | (xhci_event_cycle ? 8u : 0u);
}
static uint32_t cmd_cycle = 1;
static uint64_t ep0_tr_phys;
static volatile uint32_t *ep0_tr;
static uint32_t ep0_cycle;
static int ep0_enq = 0;
static uint64_t ep1_tr_phys;
static volatile uint32_t *ep1_tr;
static uint64_t ep1_data_phys;
static volatile uint8_t *ep1_data;
static uint16_t ep1_maxpkt = 8;
static uint8_t ep1_interval = 10;
static uint8_t ep1_num = 1;
static uint8_t ep1_id = 3;
static uint8_t ep1_ifnum = 0;

static void xhci_reset(volatile uint8_t *cap, uint8_t caplen);
static void xhci_setup_and_run(volatile uint8_t *cap, uint8_t caplen);
static void xhci_ports_init(volatile uint8_t *cap, uint8_t caplen, uint32_t hcsparams1);
static int xhci_enable_slot(volatile uint8_t *cap);
static int xhci_address_device(volatile uint8_t *cap, uint8_t caplen);
static uint8_t xhci_control_in(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, volatile uint32_t *data, uint64_t data_phys);
static uint8_t xhci_control_out(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex);
static void xhci_get_device_descriptor(volatile uint8_t *cap, uint8_t caplen);
static void xhci_get_config_descriptor(volatile uint8_t *cap, uint8_t caplen);
static uint8_t xhci_send_command(volatile uint8_t *cap, uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3);
static uint8_t xhci_prep_ep0(volatile uint8_t *cap);
static void xhci_setup_hid(volatile uint8_t *cap, uint8_t caplen);
static void xhci_configure_hid(volatile uint8_t *cap, uint8_t caplen);

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
    xhci_slot_id = xhci_enable_slot(cap);
    if (xhci_slot_id && xhci_address_device(cap, caplen)) {
        xhci_get_device_descriptor(cap, caplen);
        xhci_get_config_descriptor(cap, caplen);
        xhci_setup_hid(cap, caplen);
        xhci_configure_hid(cap, caplen);
    }
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

        xhci_connected_port = port;
        xhci_portsc = sc;

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
    xhci_dcbaap = (uint64_t *)(vmm_get_hhdm() + dcbaap_phys);
    memset((void *)xhci_dcbaap, 0, 4096);

    erst[0] = event_phys;
    erst[1] = 256;

    uint32_t rts_off = *(volatile uint32_t *)(cap + 0x18);
    volatile uint8_t *rt = (volatile uint8_t *)cap + (rts_off & ~0x1F);
    volatile uint32_t *erstsz = (volatile uint32_t *)(rt + 0x28);
    volatile uint64_t *erstba = (volatile uint64_t *)(rt + 0x30);
    volatile uint64_t *erdp = (volatile uint64_t *)(rt + 0x38);
    xhci_erdp = erdp;
    *erstsz = 1;
    *erstba = erst_phys;
    *erdp = event_phys | 8u;

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

static int xhci_enable_slot(volatile uint8_t *cap) {
    if (!cmd_ring) { serial_puts("xhci: cmd ring not set\n"); return 0; }

    uint32_t cyc = cmd_cycle;
    cmd_ring[0] = 0;
    cmd_ring[1] = 0;
    cmd_ring[2] = 0;
    cmd_ring[3] = (9u << 10) | cyc;

    cmd_ring[4] = (uint32_t)cmd_phys;
    cmd_ring[5] = (uint32_t)(cmd_phys >> 32);
    cmd_ring[6] = 0;
    cmd_ring[7] = (6u << 10) | (1u << 1) | cyc;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[0] = 0; /* DB_VALUE_HOST */
    (void)db[0];
    cmd_cycle ^= 1u;

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 33) { ok = 1; break; }
        if (type != 0 && e < 255) e++;
    }
    if (!ok) { serial_puts("xhci: enable slot no event\n"); return 0; }
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev3 = event_ring[e * 4 + 3];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint8_t slot_id = (uint8_t)(ev3 >> 24);
    serial_puts("xhci: enable slot cc=");
    serial_hex(cc); serial_puts(" slot=");
    serial_hex(slot_id); serial_puts("\n");
    return (cc == 1) ? slot_id : 0;
}

static int xhci_address_device(volatile uint8_t *cap, uint8_t caplen) {
    if (xhci_connected_port < 0 || xhci_slot_id == 0) return 0;
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);

    uint32_t pspd = (xhci_portsc >> 10) & 0xF;
    uint32_t max_pkt = 64;
    switch (pspd) {
        case 2: max_pkt = 8; break;
        case 4: case 5: case 6: case 7: max_pkt = 512; break;
    }
    uint32_t root_port = (uint32_t)(xhci_connected_port + 1);
    xhci_pspd = pspd;
    xhci_root_port = root_port;

    uint64_t in_ctx_phys = pmm_alloc_page();
    if (in_ctx_phys == 0) { serial_puts("xhci: no in ctx page\n"); return 0; }
    volatile uint32_t *in_ctx = (volatile uint32_t *)(vmm_get_hhdm() + in_ctx_phys);
    memset((void *)in_ctx, 0, 4096);

    ep0_tr_phys = pmm_alloc_page();
    if (ep0_tr_phys == 0) { serial_puts("xhci: no ep0 tr page\n"); return 0; }
    ep0_tr = (volatile uint32_t *)(vmm_get_hhdm() + ep0_tr_phys);
    memset((void *)ep0_tr, 0, 4096);

    uint64_t dev_ctx_phys = pmm_alloc_page();
    if (dev_ctx_phys == 0) { serial_puts("xhci: no dev ctx page\n"); return 0; }
    volatile uint32_t *dev_ctx = (volatile uint32_t *)(vmm_get_hhdm() + dev_ctx_phys);
    memset((void *)dev_ctx, 0, 4096);
    xhci_dev_ctx = dev_ctx;

    ep0_tr[0] = (uint32_t)ep0_tr_phys;
    ep0_tr[1] = (uint32_t)(ep0_tr_phys >> 32);
    ep0_tr[2] = 0;
    ep0_tr[3] = (6u << 10) | (1u << 1) | 1u;
    ep0_cycle = 1;
    ep0_enq = 0;

    in_ctx[1] = 0x3; /* add slot and EP0 contexts */
    /* slot context at offset 0x20, index 8 */
    in_ctx[8] = (pspd << 20) | (1u << 27); /* speed, context entries = 1 */
    in_ctx[9] = (root_port & 0xFF) << 16;
    /* EP0 context at offset 0x40, index 16 */
    in_ctx[16] = (max_pkt & 0x7FFF) << 16;
    in_ctx[17] = 8; /* avg TRB length */
    in_ctx[18] = (uint32_t)(ep0_tr_phys | 1);
    in_ctx[19] = (uint32_t)((ep0_tr_phys | 1) >> 32);

    xhci_dcbaap[xhci_slot_id] = dev_ctx_phys;

    uint32_t new_cycle = cmd_cycle;
    cmd_ring[0] = (uint32_t)in_ctx_phys;
    cmd_ring[1] = (uint32_t)(in_ctx_phys >> 32);
    cmd_ring[2] = 0;
    cmd_ring[3] = (11u << 10) | ((uint32_t)xhci_slot_id << 24) | new_cycle;

    cmd_ring[4] = (uint32_t)cmd_phys;
    cmd_ring[5] = (uint32_t)(cmd_phys >> 32);
    cmd_ring[6] = 0;
    cmd_ring[7] = (6u << 10) | (1u << 1) | new_cycle;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[0] = 0; /* DB_VALUE_HOST */
    (void)db[0];
    cmd_cycle ^= 1u;

    int e = xhci_event_idx;
    int ok = 0;
    for (int i = 0; i < 10000000; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 33) { ok = 1; break; }
        if (type != 0 && e < 255) e++;
    }
    if (!ok) { serial_puts("xhci: address no cce\n"); return 0; }
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev3 = event_ring[e * 4 + 3];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint8_t slot = (uint8_t)(ev3 >> 24);
    serial_puts("xhci: address cc=");
    serial_hex(cc); serial_puts(" slot=");
    serial_hex(slot); serial_puts("\n");
    return (cc == 1) ? slot : 0;
}

static uint8_t xhci_control_in(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, volatile uint32_t *data, uint64_t data_phys) {
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

    serial_puts("xhci: tr event cc=");
    serial_hex(cc); serial_puts(" cyc=");
    serial_hex(cyc); serial_puts(" evptr=");
    serial_hex(event_ring[e * 4 + 1]); serial_puts(" ");
    serial_hex(event_ring[e * 4 + 0]); serial_puts("\n");

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

static uint8_t xhci_control_out(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex) {
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

static void xhci_get_device_descriptor(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0) { serial_puts("xhci: no data page\n"); return; }
    volatile uint32_t *data = (volatile uint32_t *)(vmm_get_hhdm() + data_phys);
    uint8_t cc = xhci_control_in(cap, 0x80, 0x06, 0x0100, 0, 64, data, data_phys);
    serial_puts("xhci: get dev desc cc="); serial_hex(cc); serial_puts(" data=");
    serial_hex(data[1]); serial_puts(" "); serial_hex(data[0]); serial_puts("\n");
}

static uint8_t xhci_prep_ep0(volatile uint8_t *cap) {
    uint64_t deq = ep0_tr_phys + (uint64_t)ep0_enq * 4;
    uint64_t deq_val = deq | ep0_cycle;
    uint8_t scc;

    scc = xhci_send_command(cap, 0, 0, 0,
        (15u << 10) | (1u << 16) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);
    serial_puts("xhci: stop ep0 cc="); serial_hex(scc); serial_puts("\n");

    scc = xhci_send_command(cap, (uint32_t)deq_val, (uint32_t)(deq_val >> 32), 0,
        (16u << 10) | (1u << 16) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);
    serial_puts("xhci: set tr deq cc="); serial_hex(scc); serial_puts("\n");
    return scc;
}

static void xhci_setup_hid(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    uint8_t cc;

    if (xhci_prep_ep0(cap) != 1) return;
    cc = xhci_control_out(cap, 0x00, 0x09, 1, 0); /* SET_CONFIGURATION */
    serial_puts("xhci: set config cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) return;

    if (xhci_prep_ep0(cap) != 1) return;
    cc = xhci_control_out(cap, 0x21, 0x0B, 0, ep1_ifnum); /* SET_PROTOCOL (boot) */
    serial_puts("xhci: set protocol cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) return;

    if (xhci_prep_ep0(cap) != 1) return;
    cc = xhci_control_out(cap, 0x21, 0x0A, 0, ep1_ifnum); /* SET_IDLE (duration=0) */
    serial_puts("xhci: set idle cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) return;

    uint64_t report_phys = pmm_alloc_page();
    if (report_phys == 0) { serial_puts("xhci: no report page\n"); return; }
    volatile uint32_t *report = (volatile uint32_t *)(vmm_get_hhdm() + report_phys);
    memset((void *)report, 0, 4096);
    cc = xhci_control_in(cap, 0xA1, 0x01, 0x0100, ep1_ifnum, 8, report, report_phys); /* GET_REPORT */
    serial_puts("xhci: get report cc="); serial_hex(cc); serial_puts("\n");

    (void)cap;
}

static void xhci_configure_hid(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    if (xhci_slot_id == 0) return;

    uint64_t in_ctx_phys = pmm_alloc_page();
    if (in_ctx_phys == 0) { serial_puts("xhci: no in ctx cfg\n"); return; }
    volatile uint32_t *in_ctx = (volatile uint32_t *)(vmm_get_hhdm() + in_ctx_phys);
    memset((void *)in_ctx, 0, 4096);

    ep1_tr_phys = pmm_alloc_page();
    if (ep1_tr_phys == 0) { serial_puts("xhci: no ep1 tr\n"); return; }
    ep1_tr = (volatile uint32_t *)(vmm_get_hhdm() + ep1_tr_phys);
    memset((void *)ep1_tr, 0, 4096);

    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0) { serial_puts("xhci: no ep1 data\n"); return; }
    ep1_data_phys = data_phys;
    ep1_data = (volatile uint8_t *)(vmm_get_hhdm() + data_phys);

    uint32_t ep_base = 8u + (uint32_t)ep1_id * 8u;
    in_ctx[1] = 1u | (1u << (uint32_t)ep1_id); /* add slot and selected EP contexts */
    in_ctx[8] = (xhci_pspd << 20) | ((uint32_t)ep1_id << 27);
    in_ctx[9] = (xhci_root_port & 0xFF) << 16;
    in_ctx[ep_base + 0] = (uint32_t)ep1_interval << 16;
    in_ctx[ep_base + 1] = (7u << 3) | (3u << 1) | ((uint32_t)ep1_maxpkt << 16);
    in_ctx[ep_base + 2] = (uint32_t)(ep1_tr_phys | 1);
    in_ctx[ep_base + 3] = (uint32_t)((ep1_tr_phys | 1) >> 32);
    in_ctx[ep_base + 4] = (uint32_t)ep1_maxpkt | ((uint32_t)ep1_maxpkt << 16);

    uint8_t ccc = xhci_send_command(cap, (uint32_t)in_ctx_phys, (uint32_t)(in_ctx_phys >> 32), 0,
        (12u << 10) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);
    serial_puts("xhci: configure ep1 cc="); serial_hex(ccc); serial_puts("\n");
    if (ccc != 1) return;

    uint32_t cyc = 1;
    ep1_tr[0] = (uint32_t)data_phys;
    ep1_tr[1] = (uint32_t)(data_phys >> 32);
    ep1_tr[2] = ep1_maxpkt;
    ep1_tr[3] = (1u << 10) | (1u << 5) | cyc;
    ep1_tr[4] = 0;
    ep1_tr[5] = 0;
    ep1_tr[6] = 0;
    ep1_tr[7] = (8u << 10) | cyc;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[xhci_slot_id] = ep1_id;
    (void)db[xhci_slot_id];
    serial_puts("xhci: ep1 armed\n");

    /* wait a short while for a test key from QEMU monitor */
    int e = xhci_event_idx;
    int got = 0;
    for (long long i = 0; i < 2000000000LL; i++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 32) {
            uint8_t slot = (uint8_t)(ev3 >> 24);
            uint8_t ep = (uint8_t)((ev3 >> 16) & 0x1F);
            uint32_t ev2 = event_ring[e * 4 + 2];
            uint8_t cc = (uint8_t)(ev2 >> 24);
            if (slot == xhci_slot_id && ep == ep1_id && cc == 1) {
                got = 1;
                break;
            }
            if (e < 255) e++;
            continue;
        }
        if (type != 0 && e < 255) e++;
    }
    if (got) {
        xhci_advance_event(e);
        serial_puts("xhci: hid report");
        for (int i = 0; i < 8; i++) {
            serial_puts(" ");
            serial_hex(ep1_data[i]);
        }
        serial_puts("\n");
    } else {
        serial_puts("xhci: no key event\n");
    }
}

static uint8_t xhci_send_command(volatile uint8_t *cap, uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3) {
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
        if (type == 33) { ok = 1; break; }
        if (type != 0 && e < 255) e++;
    }
    if (!ok) { serial_puts("xhci: command no cce\n"); return 0; }
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    return cc;
}

static void xhci_get_config_descriptor(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0) { serial_puts("xhci: no data page\n"); return; }
    volatile uint32_t *data = (volatile uint32_t *)(vmm_get_hhdm() + data_phys);
    uint8_t cc = xhci_control_in(cap, 0x80, 0x06, 0x0200, 0, 64, data, data_phys);
    serial_puts("xhci: get config desc cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) { serial_puts("xhci: config desc failed\n"); return; }

    volatile uint8_t *b = (volatile uint8_t *)data;
    uint16_t wTotalLength = (uint16_t)(b[2] | (b[3] << 8));
    uint8_t bNumInterfaces = b[4];
    uint8_t bConfigurationValue = b[5];
    serial_puts("xhci: cfg wTotalLength="); serial_hex(wTotalLength);
    serial_puts(" bNumInterfaces="); serial_hex(bNumInterfaces);
    serial_puts(" bConfigurationValue="); serial_hex(bConfigurationValue); serial_puts("\n");

    uint8_t cur_if = 0, cur_class = 0, cur_sub = 0, cur_proto = 0;
    int got = 0;
    for (uint16_t i = 0; i + 1 < wTotalLength;) {
        uint8_t len = b[i];
        uint8_t type = b[i + 1];
        if (len == 0) break;
        if (type == 4) {
            cur_if = b[i + 2];
            cur_class = b[i + 5];
            cur_sub = b[i + 6];
            cur_proto = b[i + 7];
            serial_puts("xhci: interface ");
            serial_hex(cur_if); serial_puts(" class=");
            serial_hex(cur_class); serial_puts(" sub=");
            serial_hex(cur_sub); serial_puts(" proto=");
            serial_hex(cur_proto); serial_puts("\n");
        } else if (type == 0x21) {
            uint16_t report_len = (uint16_t)(b[i + 7] | (b[i + 8] << 8));
            serial_puts("xhci: hid desc country=");
            serial_hex(b[i + 4]); serial_puts(" report_len=");
            serial_hex(report_len); serial_puts("\n");
        } else if (type == 5) {
            uint16_t max_pkt = (uint16_t)(b[i + 4] | (b[i + 5] << 8));
            uint8_t ep_addr = b[i + 2];
            serial_puts("xhci: ep addr=");
            serial_hex(ep_addr); serial_puts(" attr=");
            serial_hex(b[i + 3]); serial_puts(" maxpkt=");
            serial_hex(max_pkt); serial_puts("\n");
            if ((ep_addr & 0x80u) && !got &&
                cur_class == 0x03u && cur_sub == 0x01u && cur_proto == 0x01u) {
                got = 1;
                ep1_num = ep_addr & 0x0Fu;
                ep1_id = (ep1_num << 1) + 1;
                ep1_maxpkt = max_pkt;
                ep1_ifnum = cur_if;
                uint8_t usbiv = b[i + 6];
                uint32_t target = (uint32_t)usbiv * 8;
                uint8_t xiv = 0;
                while (((1u << xiv) < target) && xiv < 31) xiv++;
                ep1_interval = xiv;
                serial_puts("xhci: ep1 num="); serial_hex(ep1_num);
                serial_puts(" id="); serial_hex(ep1_id);
                serial_puts(" interval="); serial_hex(ep1_interval);
                serial_puts("\n");
            }
        }
        i += len;
    }
}
