#include "elf.h"
#include "memory.h"
#include "serial.h"
#include "fs.h"
#include "scheduler.h"

#define ELF_BUF_SIZE 65536

int elf_load_into_address_space(uint64_t pml4_phys, const char* filename, uint64_t* entry_out) {
    void* buf = memory_alloc(ELF_BUF_SIZE);
    if (!buf) return -1;

    int n = fs_cat(filename, (char*)buf, ELF_BUF_SIZE);
    if (n <= 0) { memory_free(buf); return -1; }

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)buf;
    const unsigned char* ident = ehdr->e_ident;

    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        memory_free(buf); return -1;
    }
    if (ident[4] != ELFCLASS64 || ident[5] != ELFDATA2LSB) {
        memory_free(buf); return -1;
    }
    if (ehdr->e_type != ET_EXEC || ehdr->e_machine != EM_X86_64) {
        memory_free(buf); return -1;
    }

    if (entry_out) *entry_out = ehdr->e_entry;

    task_t* task = get_current_task();
    if (task) vm_region_init(task->vm_regions);

    const Elf64_Phdr* phdr = (const Elf64_Phdr*)((const uint8_t*)buf + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t offset = phdr[i].p_offset;

        if (memsz == 0) continue;

        uint64_t base = vaddr & ~0xFFFULL;
        uint64_t end = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t page = base; page < end; page += 0x1000) {
            uint64_t flags = PAGE_PRESENT | PAGE_RW | PAGE_USER;

            if (page < vaddr + filesz) {
                uint64_t page_phys = (uint64_t)memory_alloc_page();
                if (!page_phys) { memory_free(buf); return -1; }

                uint64_t copy_offset = (page < vaddr) ? 0 : (page - vaddr);
                uint64_t copy_size = 4096;
                if (copy_offset + copy_size > filesz)
                    copy_size = filesz - copy_offset;
                memory_copy((void*)page_phys, (const uint8_t*)buf + offset + copy_offset, copy_size);

                if (paging_map_4kb(pml4_phys, page, page_phys, flags) < 0) {
                    memory_free(buf);
                    return -1;
                }
            } else {
                if (task) {
                    uint64_t bss_end = page + 0x1000;
                    if (bss_end > end) bss_end = end;
                    vm_region_add(task->vm_regions, page, bss_end, PAGE_USER | PAGE_RW);
                }
            }
        }
    }

    if (task) {
        vm_region_add(task->vm_regions, 0x500000, 0x510000, PAGE_USER | PAGE_RW);
    }

    for (uint64_t page = 0x500000; page < 0x510000; page += 0x1000) {
        paging_unmap_4kb(pml4_phys, page);
    }

    memory_free(buf);
    return 0;
}

int elf_load(const void* data, uint64_t* entry) {
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)data;
    const unsigned char* ident = ehdr->e_ident;

    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F')
        return -1;
    if (ident[4] != ELFCLASS64)
        return -1;
    if (ident[5] != ELFDATA2LSB)
        return -1;
    if (ehdr->e_type != ET_EXEC)
        return -1;
    if (ehdr->e_machine != EM_X86_64)
        return -1;

    *entry = ehdr->e_entry;

    const Elf64_Phdr* phdr = (const Elf64_Phdr*)((const uint8_t*)data + ehdr->e_phoff);

    uint64_t pages_zeroed[16];
    int pages_zeroed_count = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t offset = phdr[i].p_offset;

        if (memsz == 0) continue;

        uint64_t base = vaddr & ~0x1FFFFFULL;
        uint64_t end = (vaddr + memsz + 0x1FFFFF) & ~0x1FFFFFULL;

        for (uint64_t page = base; page < end; page += 0x200000) {
            if (!memory_get_flags(page)) {
                uint64_t flags = PAGE_PRESENT | PAGE_RW | PAGE_USER;
                if (memory_map_2mb(page, page, flags) < 0) {
                    serial_write("elf_load: map failed\n", 21);
                    return -1;
                }
            } else {
                uint64_t f = memory_get_flags(page);
                if (!(f & PAGE_USER)) {
                    serial_write("elf_load: segment in supervisor area\n", 37);
                    return -1;
                }
            }

            int already = 0;
            for (int z = 0; z < pages_zeroed_count; z++) {
                if (pages_zeroed[z] == page) { already = 1; break; }
            }
            if (!already) {
                memory_set((void*)page, 0, 0x200000);
                pages_zeroed[pages_zeroed_count++] = page;
            }
        }

        memory_copy((void*)vaddr, (const uint8_t*)data + offset, filesz);
    }

    return 0;
}
