#include <stdint.h>
#include <string.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/xhci.h"
#include "xhci_internal.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

uint8_t xhci_config_value = 1;

int xhci_enable_slot(volatile uint8_t *cap) {
    if (!cmd_ring) { serial_puts("xhci: cmd ring not set\n"); return 0; }

    uint32_t cyc = cmd_cycle;
    serial_puts("[CMD c"); serial_hex(xhci_ctrl_id);
    serial_puts(" ENABLE_SLOT cyc="); serial_hex(cyc);
    serial_puts(" cmd_phys="); serial_hex(cmd_phys);
    serial_puts("]\n");
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
    serial_puts("[DB c"); serial_hex(xhci_ctrl_id);
    serial_puts(" db=0 val=0]\n");
    db[0] = 0; /* DB_VALUE_HOST */
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
        xhci_log_event(e, "skip-enable");
        xhci_advance_event(e);
        e = xhci_event_idx;
    }
    if (!ok) { serial_puts("xhci: enable slot no event\n"); return 0; }
    xhci_log_event(e, "match-enable");
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev3 = event_ring[e * 4 + 3];
    uint32_t ev0 = event_ring[e * 4 + 0];
    uint32_t ev1 = event_ring[e * 4 + 1];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint8_t slot_id = (uint8_t)(ev3 >> 24);
    uint64_t cmd_trb_ptr = ((uint64_t)ev1 << 32) | ev0;
    xhci_slot_cc = cc;
    xhci_slot_ev2 = ev2;
    xhci_slot_ev3 = ev3;
    serial_puts("[CCE c"); serial_hex(xhci_ctrl_id);
    serial_puts(" ENABLE cc="); serial_hex(cc);
    serial_puts(" slot="); serial_hex(slot_id);
    serial_puts(" cmd_trb="); serial_hex(cmd_trb_ptr);
    serial_puts(" exp="); serial_hex(cmd_phys);
    serial_puts("]\n");
    return (cc == 1) ? slot_id : 0;
}

int xhci_address_device(volatile uint8_t *cap, uint8_t caplen) {
    if (xhci_connected_port < 0 || xhci_slot_id == 0) return 0;
    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);

    uint32_t pspd = (xhci_portsc >> 10) & 0xF;
    uint32_t max_pkt = 64;
    switch (pspd) {
        case 2: max_pkt = 8; break;      /* Low Speed (1.5M) */
        case 4: case 5: case 6: case 7: max_pkt = 512; break; /* SuperSpeed */
        default: max_pkt = 64; break;    /* Full Speed (1) and High Speed (3) */
    }
    /* Context size: 32 bytes (CSZ=0) or 64 bytes (CSZ=1) per HCCPARAMS1 bit 2 */
    uint32_t ctx_dwords = xhci_ctx_size ? 16 : 8; /* 16 or 8 dwords per context */
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
    /* Link TRB at end of ring (TRB 255 = dword 1020) wrapping to start */
    ep0_tr[1020] = (uint32_t)ep0_tr_phys;
    ep0_tr[1021] = (uint32_t)(ep0_tr_phys >> 32);
    ep0_tr[1022] = 0;
    ep0_tr[1023] = (6u << 10) | (1u << 1) | 1u; /* Link TRB with TC, cycle=1 */
    ep0_cycle = 1;
    ep0_enq = 0;

    in_ctx[1] = 0x3; /* add slot and EP0 contexts (DW1 = Add Flags) */
    /* slot context at offset ctx_dwords*4 */
    uint32_t slot_idx = ctx_dwords;       /* 8 or 16 */
    uint32_t ep0_idx = ctx_dwords * 2;    /* 16 or 32 */
    in_ctx[slot_idx + 0] = (pspd << 20) | (1u << 27); /* speed, context entries = 1 */
    in_ctx[slot_idx + 1] = ((root_port & 0xFF) << 16);
    /* EP0 context at offset ctx_dwords*2*4 */
    in_ctx[ep0_idx + 0] = 0; /* EP0 dword0: state etc. */
    in_ctx[ep0_idx + 1] = ((max_pkt & 0x7FFF) << 16) | (4u << 3) | (3u << 1); /* max packet, EP type control, CErr=3 */
    in_ctx[ep0_idx + 2] = (uint32_t)(ep0_tr_phys | 1);
    in_ctx[ep0_idx + 3] = (uint32_t)((ep0_tr_phys | 1) >> 32);
    in_ctx[ep0_idx + 4] = 8; /* average TRB length in dword4 */

    serial_puts("xhci: slot=");
    serial_hex(xhci_slot_id);
    serial_puts(" port="); serial_hex(xhci_root_port);
    serial_puts(" speed="); serial_hex(xhci_pspd);
    serial_puts(" maxpkt="); serial_hex(max_pkt);
    serial_puts("\n");
    serial_puts("xhci: in_ctx="); serial_hex((uint32_t)in_ctx_phys);
    serial_puts(" dev_ctx="); serial_hex((uint32_t)dev_ctx_phys);
    serial_puts(" ep0="); serial_hex((uint32_t)ep0_tr_phys);
    serial_puts(" cmd="); serial_hex((uint32_t)cmd_phys);
    serial_puts("\n");
    serial_puts("[INPUT CTX c"); serial_hex(xhci_ctrl_id);
    serial_puts("]\n");
    for (int i = 0; i < 48; i++) {
        serial_puts("IC["); serial_hex(i); serial_puts("]=");
        serial_hex(in_ctx[i]); serial_puts("\n");
    }
    serial_puts("[SLOT CTX: DW0="); serial_hex(in_ctx[slot_idx + 0]);
    serial_puts(" DW1="); serial_hex(in_ctx[slot_idx + 1]);
    serial_puts("]\n");
    serial_puts("[EP0 CTX: DW0="); serial_hex(in_ctx[ep0_idx + 0]);
    serial_puts(" DW1="); serial_hex(in_ctx[ep0_idx + 1]);
    serial_puts(" DW2="); serial_hex(in_ctx[ep0_idx + 2]);
    serial_puts(" DW3="); serial_hex(in_ctx[ep0_idx + 3]);
    serial_puts(" DW4="); serial_hex(in_ctx[ep0_idx + 4]);
    serial_puts("]\n");
    {
        uint32_t s_dw0 = in_ctx[slot_idx + 0];
        uint32_t e_dw1 = in_ctx[ep0_idx + 1];
        uint32_t e_dw2 = in_ctx[ep0_idx + 2];
        serial_puts("[DECODE SLOT speed="); serial_hex((s_dw0 >> 20) & 0xF);
        serial_puts(" ctx_entries="); serial_hex((s_dw0 >> 27) & 0x1F);
        serial_puts(" root_port="); serial_hex((in_ctx[slot_idx + 1] >> 16) & 0xFF);
        serial_puts("]\n");
        serial_puts("[DECODE EP0 type="); serial_hex((e_dw1 >> 3) & 0x7);
        serial_puts(" cerr="); serial_hex((e_dw1 >> 1) & 0x3);
        serial_puts(" maxpkt="); serial_hex((e_dw1 >> 16) & 0x7FFF);
        serial_puts(" dcs="); serial_hex(e_dw2 & 1u);
        serial_puts(" deq="); serial_hex((uint64_t)(e_dw2 & ~0xFu) | ((uint64_t)in_ctx[ep0_idx + 3] << 32));
        serial_puts(" avg_trb="); serial_hex(in_ctx[ep0_idx + 4]);
        serial_puts("]\n");
    }

    xhci_dcbaap[xhci_slot_id] = dev_ctx_phys;

    uint32_t new_cycle = cmd_cycle;
    serial_puts("[CMD c"); serial_hex(xhci_ctrl_id);
    serial_puts(" ADDR_DEV slot="); serial_hex(xhci_slot_id);
    serial_puts(" in_ctx="); serial_hex(in_ctx_phys);
    serial_puts(" cyc="); serial_hex(new_cycle);
    serial_puts("]\n");
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
    serial_puts("[DB c"); serial_hex(xhci_ctrl_id);
    serial_puts(" db=0 val=0]\n");
    db[0] = 0; /* DB_VALUE_HOST */
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
        xhci_log_event(e, "skip-addr");
        xhci_advance_event(e);
        e = xhci_event_idx;
    }
    if (!ok) { serial_puts("xhci: address no cce\n"); return 0; }
    xhci_log_event(e, "match-addr");
    xhci_advance_event(e);

    uint32_t ev2 = event_ring[e * 4 + 2];
    uint32_t ev3 = event_ring[e * 4 + 3];
    uint32_t ev0 = event_ring[e * 4 + 0];
    uint32_t ev1 = event_ring[e * 4 + 1];
    uint8_t cc = (uint8_t)(ev2 >> 24);
    uint8_t slot = (uint8_t)(ev3 >> 24);
    uint64_t cmd_trb_ptr = ((uint64_t)ev1 << 32) | ev0;
    serial_puts("[CCE c"); serial_hex(xhci_ctrl_id);
    serial_puts(" ADDR cc="); serial_hex(cc);
    serial_puts(" slot="); serial_hex(slot);
    serial_puts(" cmd_trb="); serial_hex(cmd_trb_ptr);
    serial_puts(" exp="); serial_hex(cmd_phys);
    serial_puts("]\n");
    xhci_addr_cc = cc;
    if (cc == 1) {
        xhci_dev_ctx = dev_ctx;
        serial_puts("[DEV CTX after addr]\n");
        for (int i = 0; i < 32; i++) {
            serial_puts("DC["); serial_hex(i); serial_puts("]=");
            serial_hex(dev_ctx[i]); serial_puts("\n");
        }
    }
    return (cc == 1) ? slot : 0;
}

void xhci_get_device_descriptor(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0) { serial_puts("xhci: no data page\n"); return; }
    volatile uint32_t *data = (volatile uint32_t *)(vmm_get_hhdm() + data_phys);
    uint8_t cc = xhci_control_in(cap, 0x80, 0x06, 0x0100, 0, 64, data, data_phys);
    if (xhci_first_dev_cc == 0) xhci_first_dev_cc = cc;
    serial_puts("xhci: get dev desc cc="); serial_hex(cc); serial_puts("\n");
}

void xhci_get_config_descriptor(volatile uint8_t *cap, uint8_t caplen) {
    (void)caplen;
    uint64_t data_phys = pmm_alloc_page();
    if (data_phys == 0) { serial_puts("xhci: no data page\n"); return; }
    volatile uint32_t *data = (volatile uint32_t *)(vmm_get_hhdm() + data_phys);
    uint8_t cc = xhci_control_in(cap, 0x80, 0x06, 0x0200, 0, 64, data, data_phys);
    if (xhci_first_cfg_cc == 0) xhci_first_cfg_cc = cc;
    serial_puts("xhci: get config desc cc="); serial_hex(cc); serial_puts("\n");
    if (cc != 1 && cc != 13) { serial_puts("xhci: config desc failed\n"); return; }

    volatile uint8_t *b = (volatile uint8_t *)data;
    uint16_t wTotalLength = (uint16_t)(b[2] | (b[3] << 8));
    if (wTotalLength > 64) {
        if (wTotalLength > 4096) wTotalLength = 4096;
        cc = xhci_control_in(cap, 0x80, 0x06, 0x0200, 0, wTotalLength, data, data_phys);
        if (cc != 1 && cc != 13) { serial_puts("xhci: full config desc failed\n"); return; }
    }
    uint8_t bNumInterfaces = b[4];
    uint8_t bConfigurationValue = b[5];
    xhci_config_value = bConfigurationValue;
    if (xhci_config_value == 0) xhci_config_value = 1;
    serial_puts("xhci: cfg wTotalLength="); serial_hex(wTotalLength);    serial_puts(" bNumInterfaces="); serial_hex(bNumInterfaces);
    serial_puts(" bConfigurationValue="); serial_hex(bConfigurationValue); serial_puts("\n");

    uint8_t cur_if = 0, cur_class = 0, cur_sub = 0, cur_proto = 0;
    int got = 0, fb_id = 0;
    uint8_t fb_num = 0, fb_if = 0xFF, fb_interval = 0;
    uint16_t fb_maxpkt = 0;
    for (uint16_t i = 0; i + 1 < wTotalLength;) {
        uint8_t len = b[i];
        uint8_t type = b[i + 1];
        if (len == 0) break;
        if (type == 4) {
            cur_if = b[i + 2];
            cur_class = b[i + 5];
            cur_sub = b[i + 6];
            cur_proto = b[i + 7];
            if (xhci_first_if_class == 0) {
                xhci_first_if_class = cur_class;
                xhci_first_if_sub = cur_sub;
                xhci_first_if_proto = cur_proto;
            }
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
            uint8_t attr = b[i + 3];
            serial_puts("xhci: ep addr=");
            serial_hex(ep_addr); serial_puts(" attr=");
            serial_hex(attr); serial_puts(" maxpkt=");
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
            } else if ((ep_addr & 0x80u) && !got && !fb_id &&
                       cur_class == 0x03u && (attr & 0x03u) == 0x03u) {
                fb_num = ep_addr & 0x0Fu;
                fb_id = (fb_num << 1) + 1;
                fb_maxpkt = max_pkt;
                fb_if = cur_if;
                uint8_t usbiv = b[i + 6];
                uint32_t target = (uint32_t)usbiv * 8;
                uint8_t xiv = 0;
                while (((1u << xiv) < target) && xiv < 31) xiv++;
                fb_interval = xiv;
                serial_puts("xhci: fallback ep num="); serial_hex(fb_num);
                serial_puts(" id="); serial_hex(fb_id);
                serial_puts("\n");
            }
        }
        i += len;
    }
    if (!got && fb_id) {
        ep1_num = fb_num;
        ep1_id = fb_id;
        ep1_maxpkt = fb_maxpkt;
        ep1_ifnum = fb_if;
        ep1_interval = fb_interval;
        serial_puts("xhci: using non-boot HID fallback\n");
    }
}



