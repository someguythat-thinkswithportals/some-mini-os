#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char* buf, uint64_t len);
void serial_write_hex64(uint64_t v);
void serial_write_hex32(uint32_t v);
char serial_read(void);
int serial_data_available(void);

#endif
