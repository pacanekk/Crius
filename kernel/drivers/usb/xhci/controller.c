#include <stdint.h>
#include <string.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "drivers/keyboard.h"
#include "xhci_internal.h"
#include "arch/apic.h"
#include "arch/idt.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "drivers/framebuffer.h"

extern void irq34(void);

#define XHCI_CLASS      0x0C
#define XHCI_SUBCLASS   0x03
#define XHCI_PROG_IF    0x30

#define XHCI_MAX_DEVICES 8

struct xhci_device_info {
    int port;
    uint32_t sc;
};

static struct xhci_device_info xhci_devices[XHCI_MAX_DEVICES];
static int xhci_device_count = 0;

uint64_t *xhci_dcbaap;
volatile uint32_t *xhci_dev_ctx;
volatile uint32_t *xhci_iman;
int xhci_connected_port = -1;
uint32_t xhci_portsc = 0;
uint8_t xhci_slot_id = 0;
uint32_t xhci_pspd = 0;
uint32_t xhci_root_port = 0;
volatile uint8_t *xhci_cap;
uint8_t xhci_first_if_class = 0;
uint8_t xhci_first_if_sub = 0;
uint8_t xhci_first_if_proto = 0;
uint8_t xhci_first_dev_cc = 0;
uint8_t xhci_first_cfg_cc = 0;
uint8_t xhci_addr_cc = 0;
uint8_t xhci_slot_cc = 0;
uint8_t xhci_set_cfg_cc = 0;
uint8_t xhci_dev_slot_cc[8];
uint8_t xhci_dev_addr_cc[8];
uint8_t xhci_dev_port[8];
uint8_t xhci_dev_spd[8];
uint32_t xhci_slot_ev2 = 0;
uint32_t xhci_slot_ev3 = 0;
uint8_t xhci_set_proto_cc = 0;
uint8_t xhci_set_idle_cc = 0;
uint8_t xhci_get_report_cc = 0;
uint8_t xhci_cfg_ep_cc = 0;
int xhci_total_devices = 0;
static void xhci_reset(volatile uint8_t *cap, uint8_t caplen);
static void xhci_setup_and_run(volatile uint8_t *cap, uint8_t caplen);
static void xhci_ports_init(volatile uint8_t *cap, uint8_t caplen, uint32_t hcsparams1);
static void xhci_ownership_handoff(volatile uint8_t *cap, uint32_t hccparams1);
uint8_t xhci_control_in(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, volatile uint32_t *data, uint64_t data_phys);
uint8_t xhci_control_out(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex);
uint8_t xhci_send_command(volatile uint8_t *cap, uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3);
uint8_t xhci_prep_ep0(volatile uint8_t *cap);
static uint64_t pci_bar_addr(const struct pci_device *d, int bar) {
    if (bar >= 6) return 0;
    uint32_t lo = d->bars[bar];
    if (lo & 1) return 0;             /* I/O BAR, not memory */
    uint32_t type = lo & 0x6;
    uint32_t addr = lo & ~0xF;
    if (type == 0x4 && (bar + 1) < 6) { /* 64-bit */
        uint64_t hi = d->bars[bar + 1];
        return (hi << 32) | addr;
    }
    return addr;
}

static void xhci_ownership_handoff(volatile uint8_t *cap, uint32_t hccparams1) {
    uint32_t xecp = (hccparams1 >> 16) & 0xFFFFu;
    if (!xecp) return;

    uint32_t off = xecp * 4u;
    while (off && off < 0x1000u) {
        volatile uint32_t *reg = (volatile uint32_t *)(cap + off);
        uint32_t dw = *reg;
        uint8_t id = dw & 0xFFu;
        uint8_t next = (dw >> 8) & 0xFFu;

        if (id == 1) { /* USB Legacy Support Capability */
            if (dw & (1u << 16)) { /* HC BIOS Owned Semaphore set */
                *reg = dw | 1u;      /* request OS ownership */
                int ok = 0;
                for (int i = 0; i < 1000000; i++) {
                    uint32_t v = *reg;
                    if ((v & (1u << 16)) == 0 && (v & 1u)) { ok = 1; break; }
                }
                if (!ok) serial_puts("xhci: bios ownership not released\n");
                /* disable firmware SMIs */
                *(volatile uint32_t *)(cap + off + 4) = 0;
            }
            break;
        }
        if (!next) break;
        off += next * 4u;
    }
}

static uint32_t xhci_mfindex(volatile uint8_t *cap) {
    uint32_t rts_off = *(volatile uint32_t *)(cap + 0x18);
    volatile uint8_t *rt = cap + (rts_off & ~0x1Fu);
    return *(volatile uint32_t *)(rt + 0x18) & 0x3FFFu;
}

static void fb_print_hex(uint32_t v) {
    if (v == 0) { fb_putc('0', 0x00FFFFFF, 0x00000000); return; }
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t n = (v >> i) & 0xF;
        if (n) started = 1;
        if (started) {
            char c = n < 10 ? '0' + n : 'a' + n - 10;
            fb_putc(c, 0x00FFFFFF, 0x00000000);
        }
    }
}

static void fb_print_hex8(uint32_t v) {
    for (int i = 4; i >= 0; i -= 4) {
        uint8_t n = (v >> i) & 0xF;
        char c = n < 10 ? '0' + n : 'a' + n - 10;
        fb_putc(c, 0x00FFFFFF, 0x00000000);
    }
}

static void tsc_calibrate_xhci(volatile uint8_t *cap) {
    if (tsc_per_ms) return;
    uint32_t start = xhci_mfindex(cap);
    uint64_t t0 = tsc_read();
    while (((xhci_mfindex(cap) - start) & 0x3FFFu) < 80)
        __asm__ volatile ("pause");
    uint64_t t1 = tsc_read();
    tsc_per_ms = (t1 - t0) / 10;
    if (tsc_per_ms == 0) tsc_per_ms = 1;
    serial_puts("xhci: tsc_per_ms=");
    serial_hex((uint32_t)tsc_per_ms); serial_puts("\n");
}

static void xhci_mdelay(volatile uint8_t *cap, uint32_t ms) {
    tsc_calibrate_xhci(cap);
    tsc_mdelay(ms);
}

void xhci_init(void) {
    int xhcis[8];
    int n = 0;
    for (int i = 0; i < pci_device_count && n < 8; i++) {
        struct pci_device *d = &pci_devices[i];
        if (d->class_code == XHCI_CLASS &&
            d->subclass == XHCI_SUBCLASS &&
            d->prog_if == XHCI_PROG_IF) {
            xhcis[n++] = i;
        }
    }
    if (n == 0) {
        serial_puts("xhci: no controller found\n");
        fb_puts("Crius: no xHCI\n", 0x00FFFFFF, 0x00000000);
        return;
    }

    serial_puts("xhci: controllers="); serial_hex(n); serial_puts("\n");
    idt_set_gate(0x22, (void *)irq34, 0x8E);
    fb_puts("Crius: scanning xHCI...\n", 0x00FFFFFF, 0x00000000);

    volatile uint32_t *prev_iman = NULL;
    for (int c = 0; c < n; c++) {
        struct pci_device *d = &pci_devices[xhcis[c]];
        serial_puts("xhci: trying ");
        serial_hex(d->bus); serial_puts(":");
        serial_hex(d->dev); serial_puts(":");
        serial_hex(d->func); serial_puts("\n");

        uint64_t bar = pci_bar_addr(d, 0);
        if (bar == 0) {
            serial_puts("xhci: invalid or I/O BAR\n");
            continue;
        }

        /* Enable Memory Space (bit 1) + Bus Master (bit 2) + SERR (bit 8) */
        uint32_t cmd_reg = pci_read_config16(d->bus, d->dev, d->func, 0x04);
        cmd_reg |= 0x0006; /* Memory Space + Bus Master */
        pci_write_config32(d->bus, d->dev, d->func, 0x04, cmd_reg);

        uint64_t vbase = 0xFFFFFFFFA0000000UL + (uint64_t)c * 0x10000;
        if (vmm_map_range(vmm_current_pml4(), vbase, bar & ~0xFFFUL, 16,
                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_CACHE) < 0) {
            serial_puts("xhci: vmm_map_range failed\n");
            continue;
        }

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

        xhci_cap = cap;
        xhci_ownership_handoff(cap, hccparams1);
        xhci_reset(cap, caplen);
        xhci_setup_and_run(cap, caplen);

        if (c > 0 && prev_iman) *prev_iman = 0; /* disable previous controller IRQs */
        prev_iman = xhci_iman;

        serial_puts("xhci: intline=");
        serial_hex(d->interrupt_line);
        serial_puts("\n");
        if (d->interrupt_line == 0xFF) {
            serial_puts("xhci: no INTx routing\n");
        } else {
            ioapic_set_redirect(d->interrupt_line, 0x22);
        }

        xhci_ports_init(cap, caplen, hcsparams1);

        if (xhci_device_count == 0) {
            serial_puts("xhci: no devices on this controller, skipping\n");
            continue;
        }

        /* drain any pending events (Port Status Change etc.) before enumeration */
        xhci_drain_events();

        xhci_slot_id = 0;
        for (int i = 0; i < xhci_device_count; i++) {
            /* reset per-device state */
            ep1_id = 0;
            ep1_ifnum = 0xFF;
            ep1_num = 0;
            ep1_maxpkt = 8;
            ep1_interval = 10;
            xhci_first_if_class = 0;
            xhci_first_if_sub = 0;
            xhci_first_if_proto = 0;
            xhci_first_dev_cc = 0;
            xhci_first_cfg_cc = 0;
            xhci_addr_cc = 0;
            xhci_slot_cc = 0;
            xhci_set_cfg_cc = 0;
            xhci_set_proto_cc = 0;
            xhci_set_idle_cc = 0;
            xhci_get_report_cc = 0;
            xhci_cfg_ep_cc = 0;

            xhci_drain_events();

            xhci_connected_port = xhci_devices[i].port;
            xhci_portsc = xhci_devices[i].sc;
            xhci_dev_port[i] = (uint8_t)xhci_connected_port;
            xhci_dev_spd[i] = (uint8_t)((xhci_portsc >> 10) & 0xF);
            uint8_t slot = xhci_enable_slot(cap);
            xhci_dev_slot_cc[i] = xhci_slot_cc;
            if (!slot) continue;
            xhci_slot_id = slot;
            if (!xhci_address_device(cap, caplen)) { xhci_dev_addr_cc[i] = xhci_addr_cc; continue; }
            xhci_dev_addr_cc[i] = xhci_addr_cc;
            xhci_get_device_descriptor(cap, caplen);
            xhci_get_config_descriptor(cap, caplen);
            if (ep1_id != 0 && ep1_ifnum != 0xFF) {
                xhci_setup_hid(cap, caplen);
                xhci_configure_hid(cap, caplen);
                if (usb_kbd_present) break;
            }
        }
        if (usb_kbd_present) break;
    }

    if (!usb_kbd_present) {
        serial_puts("xhci: no keyboard found\n");
        if (xhci_total_devices == 0) {
            fb_puts("Crius: no USB devices on xHCI\n", 0x00FFFFFF, 0x00000000);
        } else {
            fb_puts("Crius: xHCI ", 0x00FFFFFF, 0x00000000);
            fb_print_hex(xhci_total_devices);
            fb_puts(" devs\n", 0x00FFFFFF, 0x00000000);
            for (int i = 0; i < xhci_device_count && i < 8; i++) {
                fb_puts("  d", 0x00FFFFFF, 0x00000000);
                fb_putc('0' + i, 0x00FFFFFF, 0x00000000);
                fb_puts(": port=", 0x00FFFFFF, 0x00000000);
                fb_print_hex8(xhci_dev_port[i]);
                fb_puts(" spd=", 0x00FFFFFF, 0x00000000);
                fb_print_hex8(xhci_dev_spd[i]);
                fb_puts(" sCC=", 0x00FFFFFF, 0x00000000);
                fb_print_hex8(xhci_dev_slot_cc[i]);
                fb_puts(" aCC=", 0x00FFFFFF, 0x00000000);
                fb_print_hex8(xhci_dev_addr_cc[i]);
                fb_puts("\n", 0x00FFFFFF, 0x00000000);
            }
            fb_puts("  ev: sEv2=", 0x00FFFFFF, 0x00000000);
            fb_print_hex(xhci_slot_ev2);
            fb_puts(" sEv3=", 0x00FFFFFF, 0x00000000);
            fb_print_hex(xhci_slot_ev3);
            fb_puts("\n", 0x00FFFFFF, 0x00000000);
            fb_puts("  class=", 0x00FFFFFF, 0x00000000);
            fb_print_hex8(xhci_first_if_class);
            fb_putc('/', 0x00FFFFFF, 0x00000000);
            fb_print_hex8(xhci_first_if_sub);
            fb_putc('/', 0x00FFFFFF, 0x00000000);
            fb_print_hex8(xhci_first_if_proto);
            fb_puts(" setCfg=", 0x00FFFFFF, 0x00000000);
            fb_print_hex8(xhci_set_cfg_cc);
            fb_puts(" cfgEp=", 0x00FFFFFF, 0x00000000);
            fb_print_hex8(xhci_cfg_ep_cc);
            fb_puts("\n", 0x00FFFFFF, 0x00000000);
        }
    } else {
        fb_puts("Crius: USB keyboard ready\n", 0x00FFFFFF, 0x00000000);
    }

    if (xhci_cap) xhci_mdelay(xhci_cap, 3000);
}

static void xhci_reset(volatile uint8_t *cap, uint8_t caplen) {
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);
    int ok = 0;

    /* wait until the controller is ready (CNR = 0) */
    for (int i = 0; i < 1000000; i++) {
        if (!(op[1] & (1u << 11))) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout CNR=0\n"); return; }

    /* stop if running */
    if (op[0] & 1u) {
        op[0] &= ~1u;
        ok = 0;
        for (int i = 0; i < 1000000; i++) {
            if (op[1] & 1u) { ok = 1; break; }
        }
        if (!ok) { serial_puts("xhci: timeout HCHalted\n"); return; }
    }

    /* reset */
    op[0] = (1u << 1);
    ok = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(op[0] & (1u << 1))) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout HCRST\n"); return; }

    /* ready after reset */
    ok = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(op[1] & (1u << 11))) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout CNR=0 after reset\n"); return; }

    serial_puts("xhci: reset ok\n");
}

static void xhci_ports_init(volatile uint8_t *cap, uint8_t caplen, uint32_t hcsparams1) {
    xhci_device_count = 0;
    uint8_t max_ports = (hcsparams1 >> 24) & 0xFF;
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);

    serial_puts("xhci: max_ports=");
    serial_hex(max_ports); serial_puts("\n");

    for (int port = 0; port < max_ports; port++) {
        volatile uint32_t *portsc = &op[0x100 + port * 4];

        /* enable port power */
        *portsc = (1u << 9);

        int connected = 0;
        for (int w = 0; w < 20; w++) {
            uint32_t sc = *portsc;
            if (sc & (1u << 17))            /* clear CSC */
                *portsc = (1u << 9) | (1u << 17);
            if (sc & 1u) { connected = 1; break; }
            xhci_mdelay(cap, 10); /* 10 ms * 20 = 200 ms */
        }

        uint32_t sc = *portsc;
        serial_puts("xhci: port ");
        serial_hex(port); serial_puts(" sc=");
        serial_hex(sc); serial_puts("\n");

        if (!connected) continue; /* no device */

        if (xhci_device_count < XHCI_MAX_DEVICES) {
            xhci_devices[xhci_device_count].port = port;
            xhci_devices[xhci_device_count].sc = sc;
            xhci_device_count++;
            xhci_total_devices++;
        }

        serial_puts("xhci: port ");
        serial_hex(port); serial_puts(" device connected\n");

        /* port reset */
        *portsc = (1u << 9) | (1u << 4);
        int ok = 0;
        for (int r = 0; r < 50; r++) {
            uint32_t s = *portsc;
            if (!(s & (1u << 4)) && (s & (1u << 1))) { ok = 1; break; }
            xhci_mdelay(cap, 2); /* 2 ms * 50 = 100 ms */
        }
        uint32_t sc2 = *portsc;
        serial_puts("xhci: port ");
        serial_hex(port); serial_puts(" after pr sc=");
        serial_hex(sc2); serial_puts("\n");
        if (ok) {
            uint32_t s = *portsc;
            *portsc = (1u << 9) | (1u << 21); /* clear PLC, keep power */
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

    /* Read HCSParams1 for MaxSlots and HCSParams2 for scratchpad buffers */
    uint32_t hcsparams1 = *(volatile uint32_t *)(cap + 0x04);
    uint32_t hcsparams2 = *(volatile uint32_t *)(cap + 0x08);
    uint8_t max_slots = (uint8_t)(hcsparams1 & 0xFF);
    uint32_t max_spb = (hcsparams2 >> 8) & 0x1FFF; /* Max Scratchpad Buffers */

    serial_puts("xhci: max_slots="); serial_hex(max_slots);
    serial_puts(" max_spb="); serial_hex(max_spb); serial_puts("\n");

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

    /* Set up scratchpad buffers if controller requires them (xHCI spec 4.20) */
    if (max_spb > 0) {
        /* Scratchpad Buffer Array: array of uint64_t pointers, one per scratchpad buffer */
        uint64_t spb_array_phys = pmm_alloc_page();
        if (spb_array_phys == 0) { serial_puts("xhci: no spb array page\n"); return; }
        volatile uint64_t *spb_array = (volatile uint64_t *)(vmm_get_hhdm() + spb_array_phys);
        memset((void *)spb_array, 0, 4096);

        for (uint32_t i = 0; i < max_spb; i++) {
            uint64_t buf_phys = pmm_alloc_page();
            if (buf_phys == 0) { serial_puts("xhci: no spb buf page\n"); break; }
            spb_array[i] = buf_phys;
        }
        /* DCBAA slot 0 must point to the Scratchpad Buffer Array */
        xhci_dcbaap[0] = spb_array_phys;
        serial_puts("xhci: scratchpad buffers allocated: "); serial_hex(max_spb); serial_puts("\n");
    }

    erst[0] = event_phys;
    erst[1] = 256;

    uint32_t rts_off = *(volatile uint32_t *)(cap + 0x18);
    volatile uint8_t *rt = (volatile uint8_t *)cap + (rts_off & ~0x1F);
    volatile uint32_t *erstsz = (volatile uint32_t *)(rt + 0x28);
    volatile uint64_t *erstba = (volatile uint64_t *)(rt + 0x30);
    volatile uint64_t *erdp = (volatile uint64_t *)(rt + 0x38);
    volatile uint32_t *iman = (volatile uint32_t *)(rt + 0x20);
    xhci_erdp = erdp;
    xhci_iman = iman;
    *erstsz = 1;
    *erstba = erst_phys;
    *erdp = event_phys; /* DESI=0 */
    xhci_event_idx = 0;
    xhci_event_cycle = 1;
    cmd_cycle = 1;

    op[12] = (uint32_t)dcbaap_phys;
    op[13] = (uint32_t)(dcbaap_phys >> 32);
    op[6] = (uint32_t)(cmd_phys | 1);
    op[7] = (uint32_t)(cmd_phys >> 32);
    /* Config Register: set MaxSlotsEN to actual MaxSlots from HCSParams1 */
    op[14] = max_slots;

    *iman = (1u << 1); /* IE */

    op[0] = 1 | (1u << 2); /* RS + INTE */
    int ok = 0;
    for (int i = 0; i < 1000000; i++) {
        if (!(op[1] & 1u)) { ok = 1; break; }
    }
    if (!ok) { serial_puts("xhci: timeout HCHalted=0\n"); return; }
    serial_puts("xhci: running\n");
}

