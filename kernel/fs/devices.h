#ifndef DEVICES_H
#define DEVICES_H

#include <stdint.h>

int dev_dispatch_read(uint32_t major, uint32_t minor, char* buf, int size);
int dev_dispatch_write(uint32_t major, uint32_t minor, const char* buf, int size);

#endif