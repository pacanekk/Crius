#include <stdbool.h>
#include "arch/io.h"
#include "drivers/keyboard.h"
#include "arch/apic.h"
#include "process/scheduler.h"

#define KB_BUF_SIZE 256
static unsigned char kb_buf[KB_BUF_SIZE];
static int kb_buf_head = 0;
static int kb_buf_tail = 0;

void kb_buf_push(unsigned char c) {
    int next = (kb_buf_head + 1) % KB_BUF_SIZE;
    if (next == kb_buf_tail) return;
    kb_buf[kb_buf_head] = c;
    kb_buf_head = next;
}

unsigned char kb_buf_pop(void) {
    if (kb_buf_head == kb_buf_tail) return 0;
    unsigned char c = kb_buf[kb_buf_tail];
    kb_buf_tail = (kb_buf_tail + 1) % KB_BUF_SIZE;
    return c;
}

int kb_buf_pending(void) {
    return kb_buf_head != kb_buf_tail;
}

/* USB keyboard presence flag (set by xhci.c) */
int usb_kbd_present = 0;

static const char scancode_to_ascii[] = {
    0, 0x1B, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

static const char scancode_shift[] = {
    0, 0x1B, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool e0_prefix = false;

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85

char kb_read(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        e0_prefix = true;
        return 0;
    }

    if (e0_prefix) {
        e0_prefix = false;
        if (scancode & 0x80) return 0;
        switch (scancode) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            default: return 0;
        }
    }

    if (scancode & 0x80) {
        if ((scancode & 0x7F) == 0x2A || (scancode & 0x7F) == 0x36)
            shift_pressed = false;
        if ((scancode & 0x7F) == 0x1D)
            ctrl_pressed = false;
        return 0;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return 0;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = true;
        return 0;
    }
    if (scancode < sizeof(scancode_to_ascii)) {
        char c = shift_pressed ? scancode_shift[scancode] : scancode_to_ascii[scancode];
        if (ctrl_pressed && c >= 'a' && c <= 'z')
            c = c - 'a' + 1;
        else if (ctrl_pressed && c >= 'A' && c <= 'Z')
            c = c - 'A' + 1;
        return c;
    }
    return 0;
}

void irq_handler(uint64_t vector) {
    if (vector == 32) {
        if (usb_kbd_present) usb_kbd_poll();
        if (usb_kbd_present) usb_kbd_tick();
        scheduler_tick();
        apic_eoi();
        return;
    }
    if (vector == 0x22) {
        if (usb_kbd_present) usb_kbd_poll();
        apic_eoi();
        return;
    }
    if (vector == 33) {
        apic_eoi();
        char c = kb_read();
        if (c) {
            kb_buf_push((unsigned char)c);
        }
        return;
    }
    apic_eoi();
}

/* ===== Device callbacks ===== */

int kbd_dev_read(char *buf, size_t bufsize) {
    if (bufsize == 0) return 0;
    for (;;) {
        unsigned char c = kb_buf_pop();
        if (c) { buf[0] = (char)c; return 1; }
        __asm__ volatile ("sti; hlt");
    }
}

int kbd_dev_present(void) {
    uint8_t st = inb(0x64);
    return (st != 0xFF) || usb_kbd_present;
}
