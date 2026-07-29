ARCH ?= x86_64

NASM := nasm
CC := gcc
LD := ld
OBJCOPY := objcopy

ifeq ($(ARCH),i386)
  NASM_ELF := -f elf32
  CCFLAGS := -ffreestanding -m32 -fno-builtin -fno-stack-protector -Wall -Wextra -I. -Ikernel -Ikernel/arch/i386
  USER_CFLAGS := -ffreestanding -m32 -fno-builtin -fno-stack-protector -Wall -Wextra -I some-libc
  LDFLAGS := -m elf_i386 -T kernel/arch/i386/kernel.ld -nostdlib
  QEMU := qemu-system-i386
  USER_LD := userspace/hello_i386.ld
  STAGE2_SRC := boot/stage2_i386.asm
else
  NASM_ELF := -f elf64
  CCFLAGS := -ffreestanding -m64 -mno-red-zone -fno-builtin -fno-stack-protector -Wall -Wextra -I. -Ikernel -Ikernel/arch/x86_64
  USER_CFLAGS := -ffreestanding -m64 -mno-red-zone -fno-builtin -fno-stack-protector -Wall -Wextra -I some-libc
  LDFLAGS := -m elf_x86_64 -T kernel/arch/x86_64/kernel.ld -nostdlib
  QEMU := qemu-system-x86_64
  USER_LD := userspace/hello.ld
  STAGE2_SRC := boot/stage2.asm
endif

ENTRY_SRC := kernel/arch/$(ARCH)/entry.asm
ISR_SRC := kernel/arch/$(ARCH)/isr.asm
GDT_SRC := kernel/arch/$(ARCH)/gdt.c
IDT_SRC := kernel/arch/$(ARCH)/idt.c
MEMORY_SRC := kernel/arch/$(ARCH)/memory.c
ELF_SRC := kernel/arch/$(ARCH)/elf.c

KERNEL_ARCH_OBJS := kernel/entry.o kernel/gdt.o kernel/idt.o \
            kernel/isr.o kernel/memory.o kernel/elf.o

KERNEL_SHARED_OBJS := kernel/kernel.o kernel/vga.o kernel/keyboard.o kernel/timer.o \
            kernel/syscall.o kernel/shell.o kernel/serial.o kernel/scheduler.o \
            kernel/cmos.o kernel/pipe.o kernel/libgcc.o kernel/fs/ata.o kernel/fs/tinyfs.o kernel/fs/vfs.o

KERNEL_OBJS := $(KERNEL_ARCH_OBJS) $(KERNEL_SHARED_OBJS)

USER_PROGRAMS := shell cat ls echo grep shutdown touch uname ed

.PHONY: all kernel clean run run-serial i386 run-i386 run-serial-i386

all: some-mini-os-$(ARCH).img

kernel: kernel.elf

i386:
	$(MAKE) clean
	$(MAKE) all ARCH=i386

run-i386:
	$(MAKE) all ARCH=i386
	qemu-system-i386 -drive format=raw,file=some-mini-os-i386.img -m 64M

run-serial-i386:
	$(MAKE) all ARCH=i386
	qemu-system-i386 -drive format=raw,file=some-mini-os-i386.img -m 64M -serial stdio

boot/boot.bin: boot/boot.asm
	$(NASM) -f bin -o $@ $<

boot/stage2.bin: $(STAGE2_SRC)
	$(NASM) -f bin -o $@ $<

kernel/entry.o: $(ENTRY_SRC)
	$(NASM) $(NASM_ELF) -o $@ $<

kernel/isr.o: $(ISR_SRC)
	$(NASM) $(NASM_ELF) -o $@ $<

kernel/gdt.o: $(GDT_SRC)
	$(CC) $(CCFLAGS) -c -o $@ $<

kernel/idt.o: $(IDT_SRC)
	$(CC) $(CCFLAGS) -c -o $@ $<

kernel/memory.o: $(MEMORY_SRC)
	$(CC) $(CCFLAGS) -c -o $@ $<

kernel/elf.o: $(ELF_SRC)
	$(CC) $(CCFLAGS) -c -o $@ $<

kernel/%.o: kernel/%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

kernel/fs/%.o: kernel/fs/%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

tools/mkfs.tinyfs: tools/mkfs.tinyfs.c
	$(CC) -o $@ $<

tools/tinyfs-add: tools/tinyfs-add.c
	$(CC) -o $@ $<

some-libc/some-libc.a: some-libc/crt0.c some-libc/string.c some-libc/stdio.c some-libc/stdlib.c some-libc/file.c
	$(MAKE) -C some-libc ARCH=$(ARCH)

define build-user-program
userspace/$(1).o: userspace/$(1).c some-libc/some-libc.h
	$$(CC) $$(USER_CFLAGS) -c -o $$@ $$<

userspace/$(1).elf: userspace/$(1).o some-libc/some-libc.a $(USER_LD)
	$$(LD) -m $$(if $$(filter i386,$(ARCH)),elf_i386,elf_x86_64) -T $(USER_LD) -o $$@ userspace/$(1).o some-libc/some-libc.a -static -e _start
endef

$(foreach prog,$(USER_PROGRAMS),$(eval $(call build-user-program,$(prog))))

USER_ELFS := $(addprefix userspace/,$(addsuffix .elf,$(USER_PROGRAMS)))

kernel.elf: $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

some-mini-os-$(ARCH).img: boot/boot.bin boot/stage2.bin kernel.bin tools/mkfs.tinyfs tools/tinyfs-add $(USER_ELFS)
	dd if=/dev/zero of=$@ bs=512 count=131072 2>/dev/null
	dd if=boot/boot.bin of=$@ bs=512 conv=notrunc 2>/dev/null
	dd if=boot/stage2.bin of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=kernel.bin of=$@ bs=512 seek=3 conv=notrunc 2>/dev/null
	./tools/mkfs.tinyfs $@
	./tools/tinyfs-add $@ userspace/shell.elf bin/shell
	./tools/tinyfs-add $@ userspace/cat.elf bin/cat
	./tools/tinyfs-add $@ userspace/ls.elf bin/ls
	./tools/tinyfs-add $@ userspace/echo.elf bin/echo
	./tools/tinyfs-add $@ userspace/grep.elf bin/grep
	./tools/tinyfs-add $@ userspace/shutdown.elf bin/shutdown
	./tools/tinyfs-add $@ userspace/touch.elf bin/touch
	./tools/tinyfs-add $@ userspace/uname.elf bin/uname
	./tools/tinyfs-add $@ userspace/ed.elf bin/ed

run: some-mini-os-$(ARCH).img
	$(QEMU) -drive format=raw,file=$< -m 64M

run-serial: some-mini-os-$(ARCH).img
	$(QEMU) -drive format=raw,file=$< -m 64M -serial stdio

clean:
	rm -f *.bin *.elf *.img
	rm -f boot/*.bin boot/*.o
	rm -f kernel/*.o kernel/*.bin kernel/*.elf
	rm -f kernel/fs/*.o
	rm -f userspace/*.bin userspace/*.o userspace/*.elf
	rm -f tools/mkfs.tinyfs tools/tinyfs-add
	$(MAKE) -C some-libc clean
