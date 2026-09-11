CC := gcc
AR := ar

ARCH ?= x86_64

ifeq ($(ARCH),i386)
  CFLAGS := -ffreestanding -m32 -fno-builtin -fno-stack-protector -Wall -Wextra -I.
else
  CFLAGS := -ffreestanding -m64 -mno-red-zone -fno-builtin -fno-stack-protector -Wall -Wextra -I.
endif

OBJS := crt0.o string.o stdio.o stdlib.o file.o signal.o passwd.o
ifeq ($(ARCH),i386)
  OBJS += libgcc.o
endif

.PHONY: all clean

all: some-libc.a

some-libc.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

%.o: %.c some-libc.h syscall.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.a
