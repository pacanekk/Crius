#include <stdint.h>
#include <string.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "drivers/keyboard.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "../xhci/xhci_internal.h"

uint32_t ep1_cycle;
uint64_t ep1_tr_phys;
volatile uint32_t *ep1_tr;
uint64_t ep1_data_phys;
volatile uint8_t *ep1_data;
uint16_t ep1_maxpkt = 8;
uint8_t ep1_interval = 10;
uint8_t ep1_num = 0;
uint8_t ep1_id = 0;
uint8_t ep1_ifnum = 0xFF;

#define REPEAT_START 600
#define REPEAT_PERIOD 40

static uint8_t usb_kbd_prev[8];
static uint16_t usb_kbd_repeat[256];

static char usb_kbd_code_to_char(uint8_t code) {
    char c = 0;
    if (code >= 0x04 && code <= 0x1d) c = 'a' + (code - 0x04);
    else if (code >= 0x1e && code <= 0x27) {
        const char *nums = "1234567890";
        c = nums[code - 0x1e];
    } else if (code == 0x28) c = '\n';
    else if (code == 0x29) c = 0x1b;
    else if (code == 0x2a) c = '\b';
    else if (code == 0x2b) c = '\t';
    else if (code == 0x2c) c = ' ';
    else if (code == 0x2d) c = '-';
    else if (code == 0x2e) c = '=';
    else if (code == 0x2f) c = '[';
    else if (code == 0x30) c = ']';
    else if (code == 0x31) c = '\\';
    else if (code == 0x33) c = ';';
    else if (code == 0x34) c = '\'';
    else if (code == 0x35) c = '`';
    else if (code == 0x36) c = ',';
    else if (code == 0x37) c = '.';
    else if (code == 0x38) c = '/';
    return c;
}

void usb_kbd_tick(void) {
    if (!usb_kbd_present) return;
    uint8_t current[256] = {0};
    for (int i = 2; i < 8; i++) {
        uint8_t code = usb_kbd_prev[i];
        if (code) current[code] = 1;
    }
    for (int code = 0; code < 256; code++) {
        if (!current[code]) continue;
        usb_kbd_repeat[code]++;
        if (usb_kbd_repeat[code] >= REPEAT_START &&
            ((usb_kbd_repeat[code] - REPEAT_START) % REPEAT_PERIOD) == 0) {
            char c = usb_kbd_code_to_char(code);
            if (c) {
                serial_puts("xhci: key "); serial_putc(c); serial_puts("\n");
                kb_buf_push((unsigned char)c);
            }
        }
    }
}


void xhci_setup_hid(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    uint8_t cc;

    if (ep1_id == 0 || ep1_ifnum == 0xFF) return;
    if (xhci_prep_ep0(cap) != 1) return;
    cc = xhci_control_out(cap, 0x00, 0x09, xhci_config_value, 0); /* SET_CONFIGURATION */
    xhci_set_cfg_cc = cc;
    serial_puts("xhci: set config cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) return;

    if (xhci_prep_ep0(cap) != 1) return;
    cc = xhci_control_out(cap, 0x21, 0x0B, 0, ep1_ifnum); /* SET_PROTOCOL (boot) */
    xhci_set_proto_cc = cc;
    serial_puts("xhci: set protocol cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) serial_puts("xhci: set protocol not supported, continuing\n");

    if (xhci_prep_ep0(cap) != 1) return;
    cc = xhci_control_out(cap, 0x21, 0x0A, 0, ep1_ifnum); /* SET_IDLE (duration=0) */
    xhci_set_idle_cc = cc;
    serial_puts("xhci: set idle cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1) serial_puts("xhci: set idle not supported, continuing\n");

    uint64_t report_phys = pmm_alloc_page();
    if (report_phys == 0) { serial_puts("xhci: no report page\n"); return; }
    volatile uint32_t *report = (volatile uint32_t *)(vmm_get_hhdm() + report_phys);
    memset((void *)report, 0, 4096);
    cc = xhci_control_in(cap, 0xA1, 0x01, 0x0100, ep1_ifnum, 8, report, report_phys); /* GET_REPORT */
    xhci_get_report_cc = cc;
    serial_puts("xhci: get report cc="); serial_hex(cc); serial_puts("\n");

    (void)cap;
}

void xhci_configure_hid(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    if (xhci_slot_id == 0 || ep1_id == 0) return;

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
    in_ctx[1] = 1u | (1u << (uint32_t)ep1_id); /* add slot and selected EP contexts (DW1 = Add Flags) */
    in_ctx[8] = (xhci_pspd << 20) | ((uint32_t)ep1_id << 27);
    in_ctx[9] = (xhci_root_port & 0xFF) << 16;
    in_ctx[ep_base + 0] = (uint32_t)ep1_interval << 16;
    in_ctx[ep_base + 1] = (7u << 3) | (3u << 1) | ((uint32_t)ep1_maxpkt << 16);
    in_ctx[ep_base + 2] = (uint32_t)(ep1_tr_phys | 1);
    in_ctx[ep_base + 3] = (uint32_t)((ep1_tr_phys | 1) >> 32);
    in_ctx[ep_base + 4] = ((uint32_t)ep1_maxpkt << 16);

    uint8_t ccc = xhci_send_command(cap, (uint32_t)in_ctx_phys, (uint32_t)(in_ctx_phys >> 32), 0,
        (12u << 10) | ((uint32_t)xhci_slot_id << 24) | cmd_cycle);
    xhci_cfg_ep_cc = ccc;
    serial_puts("xhci: configure ep1 cc="); serial_hex(ccc); serial_puts("\n");
    if (ccc != 1) return;

    ep1_cycle = 1;
    ep1_tr[0] = (uint32_t)ep1_data_phys;
    ep1_tr[1] = (uint32_t)(ep1_data_phys >> 32);
    ep1_tr[2] = ep1_maxpkt;
    ep1_tr[3] = (1u << 10) | (1u << 5) | ep1_cycle;
    ep1_tr[1020] = (uint32_t)ep1_tr_phys;
    ep1_tr[1021] = (uint32_t)(ep1_tr_phys >> 32);
    ep1_tr[1022] = 0;
    ep1_tr[1023] = (6u << 10) | (1u << 1) | ep1_cycle;

    uint32_t db_off = *(volatile uint32_t *)(cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(cap + (db_off & ~0x03u));
    db[xhci_slot_id] = ep1_id;
    (void)db[xhci_slot_id];
    serial_puts("xhci: ep1 armed\n");
    usb_kbd_present = 1;
}


int usb_kbd_poll(void) {
    if (!xhci_slot_id || !ep1_tr || !event_ring || !xhci_cap) return 0;

    int e = xhci_event_idx;
    for (int attempts = 0; attempts < 256; attempts++) {
        uint32_t ev3 = event_ring[e * 4 + 3];
        uint8_t type = (uint8_t)((ev3 >> 10) & 0x3F);
        if (type == 0) return 0;
        if ((ev3 & 1u) != xhci_event_cycle) return 0;
        uint8_t slot = (uint8_t)(ev3 >> 24);
        uint8_t ep = (uint8_t)((ev3 >> 16) & 0x1F);
        uint32_t ev2 = event_ring[e * 4 + 2];
        uint8_t cc = (uint8_t)(ev2 >> 24);
        if (slot != xhci_slot_id) {
            xhci_advance_event(e);
            e = xhci_event_idx;
            continue;
        }
        xhci_advance_event(e);
        if (ep != ep1_id || (cc != 1 && cc != 6)) {
            serial_puts("xhci: report ep="); serial_hex(ep);
            serial_puts(" cc="); serial_hex(cc); serial_puts("\n");
            return 0;
        }
        break;
    }

    uint8_t new[8];
    for (int i = 0; i < 8; i++) new[i] = ep1_data[i];

    uint32_t ep_state = xhci_dev_ctx[24] & 0x7u;
    uint32_t dcs = xhci_dev_ctx[26] & 1u;
    serial_puts("xhci: rep ep_state="); serial_hex(ep_state);
    serial_puts(" dcs="); serial_hex(dcs); serial_puts(" ");
    for (int i = 0; i < 8; i++) { serial_hex(new[i]); serial_puts(" "); }
    serial_puts("\n");

    uint8_t current[256] = {0};
    for (int i = 2; i < 8; i++) {
        uint8_t code = new[i];
        if (code == 0) continue;
        current[code] = 1;
    }

    for (int i = 2; i < 8; i++) {
        uint8_t code = new[i];
        if (code == 0) continue;

        int was = 0;
        for (int j = 2; j < 8; j++) {
            if (usb_kbd_prev[j] == code) { was = 1; break; }
        }

        if (!was) {
            usb_kbd_repeat[code] = 1;
            char c = usb_kbd_code_to_char(code);
            if (c) {
                serial_puts("xhci: key "); serial_putc(c); serial_puts("\n");
                kb_buf_push((unsigned char)c);
            }
        }
    }

    for (int j = 2; j < 8; j++) {
        uint8_t code = usb_kbd_prev[j];
        if (code && !current[code]) usb_kbd_repeat[code] = 0;
    }

    for (int i = 0; i < 8; i++) usb_kbd_prev[i] = new[i];

    /* set cycle to match the current controller DCS */
    dcs = xhci_dev_ctx[26] & 1u;
    ep1_cycle = 1;
    ep1_tr[0] = (uint32_t)ep1_data_phys;
    ep1_tr[1] = (uint32_t)(ep1_data_phys >> 32);
    ep1_tr[2] = ep1_maxpkt;
    ep1_tr[3] = (1u << 10) | (1u << 5) | ep1_cycle;
    /* Link TRB at end of ring (TRB 255 = dword 1020) */
    ep1_tr[1020] = (uint32_t)ep1_tr_phys;
    ep1_tr[1021] = (uint32_t)(ep1_tr_phys >> 32);
    ep1_tr[1022] = 0;
    ep1_tr[1023] = (6u << 10) | (1u << 1) | ep1_cycle;

    uint32_t db_off = *(volatile uint32_t *)(xhci_cap + 0x14);
    volatile uint32_t *db = (volatile uint32_t *)(xhci_cap + (db_off & ~0x03u));
    db[xhci_slot_id] = ep1_id;
    (void)db[xhci_slot_id];

    if (xhci_iman) *xhci_iman = (1u << 1) | (1u << 0); /* IE + clear IP (W1C) */
    return 1;
}

