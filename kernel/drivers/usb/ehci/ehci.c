#include <stdint.h>
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/ehci.h"
#include "drivers/framebuffer.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "arch/apic.h"
#include "arch/idt.h"

#define EHCI_CLASS      0x0C
#define EHCI_SUBCLASS   0x03
#define EHCI_PROG_IF    0x20

#define EHCI_MAX_DEVICES 8

static uint64_t pci_bar_addr(const struct pci_device *d, int bar) {
    if (bar >= 6) return 0;
    uint32_t lo = d->bars[bar];
    if (lo & 1) return 0;             /* I/O BAR, not memory */
    if ((lo & 0x7) == 0x4) {          /* 64-bit memory BAR */
        uint32_t hi = d->bars[bar + 1];
        return ((uint64_t)hi << 32) | (lo & ~0xFULL);
    }
    return (uint64_t)(lo & ~0xFULL);
}

struct ehci_device {
    int port;
    uint32_t portsc;
};

static struct ehci_device ehci_devices[EHCI_MAX_DEVICES];
static int ehci_device_count = 0;

static volatile uint32_t *ehci_op = NULL;
static volatile uint8_t *ehci_cap = NULL;

static void ehci_mdelay(uint32_t ms) {
    for (volatile uint64_t i = 0; i < (uint64_t)ms * 0x20000ULL; i++)
        __asm__ volatile ("pause");
}

void ehci_init(void) {
    ehci_device_count = 0;

    struct pci_device *ehci = NULL;
    for (int i = 0; i < pci_device_count; i++) {
        struct pci_device *d = &pci_devices[i];
        if (d->class_code == EHCI_CLASS &&
            d->subclass == EHCI_SUBCLASS &&
            d->prog_if == EHCI_PROG_IF) {
            ehci = d;
            break;
        }
    }
    if (!ehci) {
        serial_puts("ehci: no EHCI controller found\n");
        return;
    }

    serial_puts("ehci: found at ");
    serial_hex(ehci->bus); serial_puts(":");
    serial_hex(ehci->dev); serial_puts(":");
    serial_hex(ehci->func); serial_puts("\n");

    uint64_t bar = pci_bar_addr(ehci, 0);
    if (bar == 0) {
        serial_puts("ehci: invalid or I/O BAR\n");
        return;
    }

    uint64_t vbase = 0xFFFFFFFFA0010000UL;
    if (vmm_map_range(vmm_current_pml4(), vbase, bar & ~0xFFFUL, 16,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_NO_CACHE) < 0) {
        serial_puts("ehci: vmm_map_range failed\n");
        return;
    }

    volatile uint8_t *cap = (volatile uint8_t *)(vbase + (bar & 0xFFFUL));
    uint8_t caplen = *(volatile uint8_t *)cap;
    uint32_t hcsparams = *(volatile uint32_t *)(cap + 0x04);
    int n_ports = hcsparams & 0xF;

    volatile uint32_t *op = (volatile uint32_t *)(cap + caplen);
    ehci_cap = cap;
    ehci_op = op;

    serial_puts("ehci: caplen="); serial_hex(caplen);
    serial_puts(" n_ports="); serial_hex(n_ports); serial_puts("\n");

    /* Stop and reset the controller. */
    op[0] = 0;                  /* USBCMD: stop */
    for (int i = 0; i < 100000; i++)
        if (op[1] & (1u << 12)) break; /* HCHalted */

    op[0] = (1u << 1);          /* USBCMD: HCRESET */
    for (int i = 0; i < 1000000; i++)
        if (!(op[0] & (1u << 1))) break;

    if (op[0] & (1u << 1)) {
        serial_puts("ehci: reset failed\n");
        fb_puts("Crius: EHCI reset failed\n", 0x00FFFFFF, 0x00000000);
        return;
    }

    /* Route all ports to EHCI. */
    op[0x10] = 1;               /* CONFIGFLAG */

    /* Allocate frame list and async list (minimal, not used yet). */
    uint64_t fl_phys = pmm_alloc_page();
    if (fl_phys == 0) { serial_puts("ehci: no fl page\n"); return; }
    volatile uint32_t *fl = (volatile uint32_t *)(vmm_get_hhdm() + fl_phys);
    for (int i = 0; i < 1024; i++) fl[i] = 1; /* T = terminate */

    uint64_t qh_phys = pmm_alloc_page();
    if (qh_phys == 0) { serial_puts("ehci: no qh page\n"); return; }
    volatile uint32_t *qh = (volatile uint32_t *)(vmm_get_hhdm() + qh_phys);
    for (int i = 0; i < 1024; i++) qh[i] = 0;

    op[0x05] = (uint32_t)fl_phys;       /* PERIODICLISTBASE */
    op[0x06] = (uint32_t)qh_phys;       /* ASYNCLISTADDR */

    op[0] = (1u << 0) | (1u << 3) | (1u << 4); /* RS, PSE, ASE */
    for (int i = 0; i < 1000000; i++)
        if (!(op[1] & (1u << 12))) break;

    /* Scan ports. */
    for (int port = 0; port < n_ports; port++) {
        volatile uint32_t *portsc = &op[0x11 + port];

        /* Power on and clear CSC/PED/OCC. */
        uint32_t s = *portsc;
        s |= (1u << 12);                    /* PP */
        s |= (1u << 1) | (1u << 3) | (1u << 5); /* W1C status bits */
        *portsc = s;

        ehci_mdelay(50);

        s = *portsc;
        serial_puts("ehci: port "); serial_hex(port);
        serial_puts(" sc="); serial_hex(s); serial_puts("\n");

        if (!(s & 1u)) continue;            /* no device */

        /* Reset the port. */
        *portsc = (1u << 12) | (1u << 8);   /* PP + PR */
        ehci_mdelay(50);
        for (int r = 0; r < 100; r++) {
            s = *portsc;
            if ((s & (1u << 2)) && !(s & (1u << 8))) break;
            ehci_mdelay(2);
        }

        s = *portsc;
        serial_puts("ehci: port "); serial_hex(port);
        serial_puts(" after reset sc="); serial_hex(s); serial_puts("\n");

        if (s & (1u << 2)) {
            if (ehci_device_count < EHCI_MAX_DEVICES) {
                ehci_devices[ehci_device_count].port = port;
                ehci_devices[ehci_device_count].portsc = s;
                ehci_device_count++;
            }
            serial_puts("ehci: port "); serial_hex(port);
            serial_puts(" device connected\n");
        } else {
            serial_puts("ehci: port "); serial_hex(port);
            serial_puts(" reset timeout\n");
        }
    }

    if (ehci_device_count == 0) {
        serial_puts("ehci: no devices\n");
        fb_puts("Crius: EHCI no devices\n", 0x00FFFFFF, 0x00000000);
    } else {
        serial_puts("ehci: devices="); serial_hex(ehci_device_count); serial_puts("\n");
        fb_puts("Crius: EHCI devices found\n", 0x00FFFFFF, 0x00000000);
    }

    ehci_mdelay(2000);
}

int ehci_kbd_poll(void) {
    (void)ehci_op;
    (void)ehci_cap;
    return 0;
}
