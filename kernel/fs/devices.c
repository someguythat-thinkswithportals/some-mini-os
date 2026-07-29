#include "devices.h"
#include "../vga.h"
#include "../keyboard.h"
#include "../serial.h"
#include "../memory.h"

int dev_null_read(char* buf, int size) {
    (void)buf; (void)size;
    return 0;
}

int dev_null_write(const char* buf, int size) {
    (void)buf;
    return size;
}

int dev_zero_read(char* buf, int size) {
    memory_set(buf, 0, size);
    return size;
}

int dev_zero_write(const char* buf, int size) {
    (void)buf;
    return size;
}

int dev_tty_write(const char* buf, int size) {
    vga_write(buf, size);
    serial_write(buf, size);
    return size;
}

int dev_dispatch_read(uint32_t major, uint32_t minor, char* buf, int size) {
    if (major == 1) {
        if (minor == 3) return dev_null_read(buf, size);
        if (minor == 5) return dev_zero_read(buf, size);
    }
    return -1;
}

int dev_dispatch_write(uint32_t major, uint32_t minor, const char* buf, int size) {
    if (major == 1) {
        if (minor == 3) return dev_null_write(buf, size);
        if (minor == 5) return dev_zero_write(buf, size);
    }
    if (major == 5 && minor == 0) return dev_tty_write(buf, size);
    return -1;
}