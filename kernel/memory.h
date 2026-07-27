#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#ifdef __x86_64__
#include "arch/x86_64/memory.h"
#else
#include "arch/i386/memory.h"
#endif

#endif
