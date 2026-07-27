#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "serial.h"
#include "scheduler.h"

#define BUF_SIZE 256

static char buf[BUF_SIZE];
static int head;
static int tail;

static const char keymap[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char keymap_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static int shift_pressed;
static int ctrl_pressed;
static int e0_prefix;

enum { ESC_NORMAL, ESC_SEEN, ESC_CSI, ESC_CSI_DIGIT } esc_state;
static char esc_param;

static void buffer_char(char c) {
    int next = (head + 1) % BUF_SIZE;
    if (next != tail) {
        buf[head] = c;
        head = next;
    }
}

static void keyboard_handler(registers_t* r) {
    (void)r;
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        e0_prefix = 1;
        return;
    }

    if (e0_prefix) {
        e0_prefix = 0;
        if (scancode & 0x80) return;
        switch (scancode) {
            case 0x48: buffer_char(KEY_UP); return;
            case 0x50: buffer_char(KEY_DOWN); return;
            case 0x4B: buffer_char(KEY_LEFT); return;
            case 0x4D: buffer_char(KEY_RIGHT); return;
            case 0x47: buffer_char(KEY_HOME); return;
            case 0x4F: buffer_char(KEY_END); return;
            case 0x53: buffer_char(KEY_DELETE); return;
        }
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = 0;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = 1;
        return;
    }
    if (scancode == 0x9D) {
        ctrl_pressed = 0;
        return;
    }

    if (scancode & 0x80) return;

    char c = 0;
    if (scancode < sizeof(keymap)) {
        if (shift_pressed)
            c = keymap_shift[scancode];
        else
            c = keymap[scancode];
    }

    if (ctrl_pressed && (c == 'c' || c == 'C')) {
        task_t* t = get_current_task();
        if (t) {
            task_t* start = t;
            do {
                if (t->pid != 0 && t->state != TASK_ZOMBIE)
                    t->signal_pending |= (1ULL << SIGINT);
                t = t->next;
            } while (t != start);
        }
        esc_state = ESC_NORMAL;
        return;
    }

    switch (esc_state) {
        case ESC_NORMAL:
            if (scancode == 0x01) {
                esc_state = ESC_SEEN;
                return;
            }
            if (c) buffer_char(c);
            break;

        case ESC_SEEN:
            if (scancode == 0x1A) {
                esc_state = ESC_CSI;
                return;
            }
            buffer_char('\x1b');
            if (c) buffer_char(c);
            esc_state = ESC_NORMAL;
            break;

        case ESC_CSI:
            if (c >= 'A' && c <= 'D') {
                buffer_char(KEY_UP + (c - 'A'));
                esc_state = ESC_NORMAL;
            } else if (c == 'H') {
                buffer_char(KEY_HOME);
                esc_state = ESC_NORMAL;
            } else if (c == 'F') {
                buffer_char(KEY_END);
                esc_state = ESC_NORMAL;
            } else if (c >= '1' && c <= '9') {
                esc_param = c;
                esc_state = ESC_CSI_DIGIT;
            } else {
                buffer_char('\x1b');
                buffer_char('[');
                if (c) buffer_char(c);
                esc_state = ESC_NORMAL;
            }
            break;

        case ESC_CSI_DIGIT:
            if (c == '~') {
                if (esc_param == '3') buffer_char(KEY_DELETE);
                else if (esc_param == '1') buffer_char(KEY_HOME);
                else if (esc_param == '4') buffer_char(KEY_END);
                else {
                    buffer_char('\x1b');
                    buffer_char('[');
                    buffer_char(esc_param);
                    buffer_char('~');
                }
            } else {
                buffer_char('\x1b');
                buffer_char('[');
                buffer_char(esc_param);
                if (c) buffer_char(c);
            }
            esc_state = ESC_NORMAL;
            break;
    }
}

void keyboard_init(void) {
    head = 0;
    tail = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    e0_prefix = 0;
    esc_state = ESC_NORMAL;
    idt_register_handler(33, keyboard_handler);
}

int keyboard_read(void) {
    while (head == tail) {
        if (serial_data_available()) {
            int c = serial_read();
            if (c == '\r') c = '\n';
            if (c == 0x7F) c = '\b';
            return c;
        }
        __asm__ volatile("sti; hlt");
    }
    int c = (unsigned char)buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}

int keyboard_data(void) {
    return head != tail;
}

void keyboard_inject(const char* str) {
    for (int i = 0; str[i]; i++) {
        int next = (head + 1) % BUF_SIZE;
        if (next == tail) break;
        buf[head] = str[i];
        head = next;
    }
}
