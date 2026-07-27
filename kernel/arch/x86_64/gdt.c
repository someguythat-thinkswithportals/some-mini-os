#include "gdt.h"

#define GDT_ENTRIES 7

static gdt_entry_t gdt[GDT_ENTRIES];
static tss_t tss;
static gdt_ptr_t gdt_ptr;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].base_low = base & 0xFFFF;
    gdt[i].base_mid = (base >> 16) & 0xFF;
    gdt[i].access = access;
    gdt[i].granularity = gran | ((limit >> 16) & 0x0F);
    gdt[i].base_high = (base >> 24) & 0xFF;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xA0);
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xA0);
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xA0);

    uint64_t tss_base = (uint64_t)&tss;
    uint16_t tss_limit = sizeof(tss_t) - 1;

    gdt[5].limit_low = tss_limit & 0xFFFF;
    gdt[5].base_low = tss_base & 0xFFFF;
    gdt[5].base_mid = (tss_base >> 16) & 0xFF;
    gdt[5].access = 0x89;
    gdt[5].granularity = (tss_limit >> 16) & 0x0F;
    gdt[5].base_high = (tss_base >> 24) & 0xFF;

    uint32_t* tss_high = (uint32_t*)&gdt[6];
    tss_high[0] = (uint32_t)(tss_base >> 32);
    tss_high[1] = 0;

    tss.iopb = sizeof(tss_t);

    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));

    __asm__ volatile(
        "push $0x08\n"
        "push $.reload\n"
        "retfq\n"
        ".reload:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        : : : "memory"
    );

    __asm__ volatile("ltr %%ax" : : "a"(0x28));
}

void tss_set_rsp0(uint64_t rsp) {
    tss.rsp[0] = rsp;
}
