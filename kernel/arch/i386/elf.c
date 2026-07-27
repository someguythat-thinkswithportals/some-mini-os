#include "elf.h"
#include "memory.h"
#include "serial.h"
#include "fs.h"
#include "scheduler.h"

#define ELF_BUF_SIZE 65536

int elf_load_into_address_space(uint32_t pd_phys, const char* filename, uint32_t* entry_out) {
    void* buf = memory_alloc(ELF_BUF_SIZE);
    if (!buf) return -1;

    int n = fs_cat(filename, (char*)buf, ELF_BUF_SIZE);
    if (n <= 0) { memory_free(buf); return -1; }

    const Elf32_Ehdr* ehdr = (const Elf32_Ehdr*)buf;
    const unsigned char* ident = ehdr->e_ident;

    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        memory_free(buf); return -1;
    }
    if (ident[4] != ELFCLASS32 || ident[5] != ELFDATA2LSB) {
        memory_free(buf); return -1;
    }
    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_386) {
        memory_free(buf); return -1;
    }

    if (entry_out) *entry_out = ehdr->e_entry;

    task_t* task = get_current_task();
    if (task) vm_region_init(task->vm_regions);

    const Elf32_Phdr* phdr = (const Elf32_Phdr*)((const uint8_t*)buf + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint32_t vaddr = phdr[i].p_vaddr;
        uint32_t memsz = phdr[i].p_memsz;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t offset = phdr[i].p_offset;

        if (memsz == 0) continue;

        uint32_t base = vaddr & ~0xFFF;
        uint32_t end = (vaddr + memsz + 0xFFF) & ~0xFFF;

        for (uint32_t page = base; page < end; page += 0x1000) {
            uint32_t flags = PAGE_PRESENT | PAGE_RW | PAGE_USER;

            if (page < vaddr + filesz) {
                uint32_t page_phys = (uint32_t)memory_alloc_page();
                if (!page_phys) { memory_free(buf); return -1; }

                uint32_t copy_offset = (page < vaddr) ? 0 : (page - vaddr);
                uint32_t copy_size = 4096;
                if (copy_offset + copy_size > filesz)
                    copy_size = filesz - copy_offset;
                memory_copy((void*)page_phys, (const uint8_t*)buf + offset + copy_offset, copy_size);

                if (paging_map_4kb(pd_phys, page, page_phys, flags) < 0) {
                    memory_free(buf);
                    return -1;
                }
            } else {
                if (task) {
                    uint32_t bss_end = page + 0x1000;
                    if (bss_end > end) bss_end = end;
                    vm_region_add(task->vm_regions, page, bss_end, PAGE_USER | PAGE_RW);
                }
            }
        }
    }

    if (task) {
        vm_region_add(task->vm_regions, 0x500000, 0x510000, PAGE_USER | PAGE_RW);
    }

    for (uint32_t page = 0x500000; page < 0x510000; page += 0x1000) {
        paging_unmap_4kb(pd_phys, page);
    }

    memory_free(buf);
    return 0;
}

int elf_load(const void* data, uint32_t* entry) {
    const Elf32_Ehdr* ehdr = (const Elf32_Ehdr*)data;
    const unsigned char* ident = ehdr->e_ident;

    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F')
        return -1;
    if (ident[4] != ELFCLASS32)
        return -1;
    if (ident[5] != ELFDATA2LSB)
        return -1;
    if (ehdr->e_type != ET_EXEC)
        return -1;
    if (ehdr->e_machine != EM_386)
        return -1;

    *entry = ehdr->e_entry;

    const Elf32_Phdr* phdr = (const Elf32_Phdr*)((const uint8_t*)data + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint32_t vaddr = phdr[i].p_vaddr;
        uint32_t memsz = phdr[i].p_memsz;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t offset = phdr[i].p_offset;

        if (memsz == 0) continue;

        uint32_t base = vaddr & ~0xFFF;
        uint32_t end = (vaddr + memsz + 0xFFF) & ~0xFFF;

        for (uint32_t page = base; page < end; page += 0x1000) {
            uint32_t page_phys = (uint32_t)memory_alloc_page();
            if (!page_phys) return -1;
            memory_set((void*)page_phys, 0, 4096);

            if (page < vaddr + filesz) {
                uint32_t copy_offset = (page < vaddr) ? 0 : (page - vaddr);
                uint32_t copy_size = 4096;
                if (copy_offset + copy_size > filesz)
                    copy_size = filesz - copy_offset;
                memory_copy((void*)page_phys, (const uint8_t*)data + offset + copy_offset, copy_size);
            }

            paging_map_4kb(PD_ADDR, page, page_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
        }
    }

    return 0;
}
