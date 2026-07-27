OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;
    .text : ALIGN(4K) { *(.text*) }
    .data : ALIGN(4K) { *(.data*) }
    .rodata : ALIGN(4K) { *(.rodata*) }
    .bss : ALIGN(4K) { *(COMMON) *(.bss*) }
}
