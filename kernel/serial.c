#include "serial.h"
#include "io.h"

#define COM1 0x3F8

static int serial_tx_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

static int serial_data_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    outb(COM1 + 4, 0x01);
}

void serial_putchar(char c) {
    while (!serial_tx_empty());
    outb(COM1, c);
}

void serial_write_hex64(uint64_t v) {
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nib = (v >> i) & 0xF;
        serial_putchar(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}

void serial_write_hex32(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nib = (v >> i) & 0xF;
        serial_putchar(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
}

void serial_write(const char* buf, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        if (buf[i] == '\n') serial_putchar('\r');
        serial_putchar(buf[i]);
    }
}

char serial_read(void) {
    while (!serial_data_ready());
    return inb(COM1);
}

int serial_data_available(void) {
    return serial_data_ready();
}
