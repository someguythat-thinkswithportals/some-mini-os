#include "idt.h"
#include "io.h"
#include "serial.h"
#include "vga.h"
#include "memory.h"
#include "scheduler.h"
#include <stddef.h>

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;
static isr_handler_t handlers[IDT_ENTRIES];

extern uint64_t isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7;
extern uint64_t isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15;
extern uint64_t isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23;
extern uint64_t isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31;
extern uint64_t irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7;
extern uint64_t irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15;
extern uint64_t isr128;

static void idt_set_entry(int i, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[i].offset_low = base & 0xFFFF;
    idt[i].selector = sel;
    idt[i].ist = 0;
    idt[i].type_attr = flags;
    idt[i].offset_mid = (base >> 16) & 0xFFFF;
    idt[i].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[i].reserved = 0;
}

static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

static void dump_hex64(uint64_t v) {
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nib = (v >> i) & 0xF;
        if (nib < 10) serial_putchar('0' + nib);
        else serial_putchar('a' + nib - 10);
    }
}

static void page_fault_handler(registers_t* r) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    uint64_t err = r->err_code;

    uint64_t pml4 = current_task ? current_task->cr3 : PML4_ADDR;

    if (paging_handle_cow_fault(pml4, cr2, err) == 0)
        return;

    if (current_task && paging_handle_demand_fault(pml4, cr2, err, current_task->vm_regions) == 0)
        return;

    serial_write("\n!!! PAGE FAULT !!!\n", 20);
    serial_write("Fault address (CR2): 0x", 22);
    dump_hex64(cr2);
    serial_write("\nError code: ", 12);
    dump_hex64(err);
    serial_write("\n  Present: ", 11);
    serial_putchar(err & 1 ? '1' : '0');
    serial_write("\n  Write:   ", 11);
    serial_putchar(err & 2 ? '1' : '0');
    serial_write("\n  User:    ", 11);
    serial_putchar(err & 4 ? '1' : '0');
    serial_write("\nRIP: 0x", 7);
    dump_hex64(r->rip);
    serial_write("\nRSP: 0x", 7);
    dump_hex64(r->rsp);
    serial_write("\nHalting.\n", 9);

    vga_writestring("!!! PAGE FAULT at 0x");
    char hex[17];
    for (int i = 0; i < 16; i++) {
        uint8_t nib = (cr2 >> (60 - i * 4)) & 0xF;
        hex[i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    hex[16] = 0;
    vga_writestring(hex);
    vga_writestring(" !!!\n");

    __asm__ volatile("hlt");
}

static void gpf_handler(registers_t* r) {
    serial_write("\n!!! GENERAL PROTECTION FAULT !!!\n", 33);
    serial_write("Error code: ", 12);
    dump_hex64(r->err_code);
    serial_write("\nRIP: 0x", 7);
    dump_hex64(r->rip);
    serial_write("\nCS: 0x", 6);
    dump_hex64(r->cs);
    serial_write("\nRSP: 0x", 7);
    dump_hex64(r->rsp);
    serial_write("\nHalting.\n", 9);

    vga_writestring("!!! GPF at RIP=0x");
    char hex[17];
    uint64_t rip = r->rip;
    for (int i = 0; i < 16; i++) {
        uint8_t nib = (rip >> (60 - i * 4)) & 0xF;
        hex[i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    hex[16] = 0;
    vga_writestring(hex);
    vga_writestring(" !!!\n");

    __asm__ volatile("hlt");
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = NULL;
    }

    uint64_t isrs[] = {
        (uint64_t)&isr0, (uint64_t)&isr1, (uint64_t)&isr2, (uint64_t)&isr3,
        (uint64_t)&isr4, (uint64_t)&isr5, (uint64_t)&isr6, (uint64_t)&isr7,
        (uint64_t)&isr8, (uint64_t)&isr9, (uint64_t)&isr10,(uint64_t)&isr11,
        (uint64_t)&isr12,(uint64_t)&isr13,(uint64_t)&isr14,(uint64_t)&isr15,
        (uint64_t)&isr16,(uint64_t)&isr17,(uint64_t)&isr18,(uint64_t)&isr19,
        (uint64_t)&isr20,(uint64_t)&isr21,(uint64_t)&isr22,(uint64_t)&isr23,
        (uint64_t)&isr24,(uint64_t)&isr25,(uint64_t)&isr26,(uint64_t)&isr27,
        (uint64_t)&isr28,(uint64_t)&isr29,(uint64_t)&isr30,(uint64_t)&isr31,
    };

    uint64_t irqs[] = {
        (uint64_t)&irq0, (uint64_t)&irq1, (uint64_t)&irq2, (uint64_t)&irq3,
        (uint64_t)&irq4, (uint64_t)&irq5, (uint64_t)&irq6, (uint64_t)&irq7,
        (uint64_t)&irq8, (uint64_t)&irq9, (uint64_t)&irq10,(uint64_t)&irq11,
        (uint64_t)&irq12,(uint64_t)&irq13,(uint64_t)&irq14,(uint64_t)&irq15,
    };

    for (int i = 0; i < 32; i++) {
        idt_set_entry(i, isrs[i], 0x08, 0x8E);
    }

    for (int i = 0; i < 16; i++) {
        idt_set_entry(32 + i, irqs[i], 0x08, 0x8E);
    }

    idt_set_entry(0x80, (uint64_t)&isr128, 0x08, 0xEF);

    pic_remap();

    handlers[14] = page_fault_handler;
    handlers[13] = gpf_handler;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    __asm__ volatile("sti");
}

void idt_register_handler(int vector, isr_handler_t handler) {
    handlers[vector] = handler;
}

void handle_interrupt(registers_t* r) {
    if (handlers[r->int_no]) {
        handlers[r->int_no](r);
    } else if (r->int_no < 32) {
        serial_putchar('E');
        serial_putchar('0' + (r->int_no / 10));
        serial_putchar('0' + (r->int_no % 10));
        serial_putchar('\n');
        const char* msgs[] = {
            "Division By Zero", "Debug", "NMI", "Breakpoint",
            "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
            "Double Fault", "Coprocessor", "Invalid TSS", "Segment Not Present",
            "Stack Fault", "GP Fault", "Page Fault", "Reserved",
            "x87 Float", "Alignment Check", "Machine Check", "SIMD Float",
            "Virtualization", "Control Protection", "Reserved", "Reserved",
            "Reserved", "Reserved", "Reserved", "Reserved",
            "Reserved", "Reserved", "Security", "Reserved"
        };
        const char* msg = msgs[r->int_no];
        int len = 0;
        while (msg[len]) len++;
        serial_write("EXCEPTION: ", 11);
        for (int i = 0; i < len; i++) serial_putchar(msg[i]);
        serial_write("\nRIP: 0x", 7);
        uint64_t rip = r->rip;
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nib = (rip >> i) & 0xF;
            serial_putchar(nib < 10 ? '0' + nib : 'a' + nib - 10);
        }
        serial_write("\nHalting.\n", 9);
        __asm__ volatile("hlt");
    }

    if (r->int_no >= 32 && r->int_no <= 47) {
        if (r->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }

    if (r->cs == 0x1B) {
        signal_deliver(r);
    }
}
