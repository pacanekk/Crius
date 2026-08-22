#ifndef XHCI_INTERNAL_H
#define XHCI_INTERNAL_H

#include <stdint.h>

extern volatile uint8_t *xhci_cap;
extern uint8_t xhci_slot_id;
extern uint32_t xhci_pspd;
extern uint32_t xhci_root_port;
extern int xhci_connected_port;
extern uint32_t xhci_portsc;
extern uint64_t *xhci_dcbaap;
extern volatile uint32_t *xhci_dev_ctx;
extern volatile uint32_t *xhci_iman;

extern uint32_t ep1_cycle;
extern uint64_t ep1_tr_phys;
extern volatile uint32_t *ep1_tr;
extern uint64_t ep1_data_phys;
extern volatile uint8_t *ep1_data;
extern uint16_t ep1_maxpkt;
extern uint8_t ep1_interval;
extern uint8_t ep1_num;
extern uint8_t ep1_id;
extern uint8_t ep1_ifnum;
extern uint8_t xhci_config_value;

extern volatile uint32_t *event_ring;
extern int xhci_event_idx;
extern uint32_t xhci_event_cycle;
extern volatile uint64_t *xhci_erdp;
extern uint64_t event_phys;

extern uint32_t cmd_cycle;
extern volatile uint32_t *cmd_ring;
extern uint64_t cmd_phys;

extern uint8_t xhci_first_if_class;
extern uint8_t xhci_first_if_sub;
extern uint8_t xhci_first_if_proto;
extern uint8_t xhci_first_dev_cc;
extern uint8_t xhci_first_cfg_cc;
extern uint8_t xhci_addr_cc;
extern uint8_t xhci_slot_cc;
extern uint8_t xhci_set_cfg_cc;

extern uint8_t xhci_dev_slot_cc[8];
extern uint8_t xhci_dev_addr_cc[8];
extern uint8_t xhci_dev_port[8];
extern uint8_t xhci_dev_spd[8];
extern uint32_t xhci_slot_ev2;
extern uint32_t xhci_slot_ev3;
extern uint8_t xhci_set_proto_cc;
extern uint8_t xhci_set_idle_cc;
extern uint8_t xhci_get_report_cc;
extern uint8_t xhci_cfg_ep_cc;
extern int xhci_total_devices;

extern uint64_t ep0_tr_phys;
extern volatile uint32_t *ep0_tr;
extern uint32_t ep0_cycle;
extern int ep0_enq;

void xhci_advance_event(int e);
void xhci_drain_events(void);
void xhci_get_device_descriptor(volatile uint8_t *cap, uint8_t caplen);
void xhci_get_config_descriptor(volatile uint8_t *cap, uint8_t caplen);
int xhci_enable_slot(volatile uint8_t *cap);
int xhci_address_device(volatile uint8_t *cap, uint8_t caplen);
uint8_t xhci_send_command(volatile uint8_t *cap, uint32_t word0, uint32_t word1, uint32_t word2, uint32_t word3);
uint8_t xhci_control_in(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength, volatile uint32_t *data, uint64_t data_phys);
uint8_t xhci_control_out(volatile uint8_t *cap, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex);
uint8_t xhci_prep_ep0(volatile uint8_t *cap);
void xhci_setup_hid(volatile uint8_t *cap, uint8_t caplen);
void xhci_configure_hid(volatile uint8_t *cap, uint8_t caplen);

#endif
