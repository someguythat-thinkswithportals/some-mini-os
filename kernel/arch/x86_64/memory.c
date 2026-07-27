#include "memory.h"
#include "serial.h"
#include "vga.h"

typedef struct block {
    size_t size;
    int free;
    struct block* next;
} block_t;

static block_t* heap_start;

void memory_page_init(void);

void memory_init(void* heap_start_ptr, size_t heap_size) {
    heap_start = (block_t*)heap_start_ptr;
    heap_start->size = heap_size - sizeof(block_t);
    heap_start->free = 1;
    heap_start->next = NULL;
    memory_page_init();
    page_ref_init();
}

void* memory_alloc(size_t size) {
    block_t* curr = heap_start;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (curr->size > size + sizeof(block_t) + 8) {
                block_t* new_block = (block_t*)((uint8_t*)curr + sizeof(block_t) + size);
                new_block->size = curr->size - size - sizeof(block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                curr->size = size;
                curr->next = new_block;
            }
            curr->free = 0;
            return (uint8_t*)curr + sizeof(block_t);
        }
        curr = curr->next;
    }
    return NULL;
}

static void heap_merge(void) {
    block_t* curr = heap_start;
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(block_t) + curr->next->size;
            curr->next = curr->next->next;
            continue;
        }
        curr = curr->next;
    }
}

void memory_free(void* ptr) {
    if (!ptr) return;
    block_t* block = (block_t*)((uint8_t*)ptr - sizeof(block_t));
    block->free = 1;
    heap_merge();
}

void* memory_copy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void memory_set(void* dest, int val, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
}

static uint64_t page_alloc_cur;
static uint64_t page_alloc_end;

void memory_page_init(void) {
    page_alloc_cur = 0x600000;
    page_alloc_end = 0x800000;
}

void* memory_alloc_page(void) {
    if (page_alloc_cur >= page_alloc_end) return NULL;
    void* addr = (void*)page_alloc_cur;
    memory_set(addr, 0, 4096);
    page_alloc_cur += 4096;
    return addr;
}

// ---- Physical Page Reference Counting ----

#define PAGE_ALLOC_START 0x600000
#define PAGE_ALLOC_END   0x800000
#define MAX_PHYS_PAGES   ((PAGE_ALLOC_END - PAGE_ALLOC_START) / 4096)

static uint32_t page_refcounts[MAX_PHYS_PAGES];

static int phys_to_idx(uint64_t phys) {
    if (phys < PAGE_ALLOC_START || phys >= PAGE_ALLOC_END) return -1;
    return (int)((phys - PAGE_ALLOC_START) / 4096);
}

void page_ref_init(void) {
    for (int i = 0; i < MAX_PHYS_PAGES; i++)
        page_refcounts[i] = 0;
}

void page_ref_inc(uint64_t phys) {
    int idx = phys_to_idx(phys);
    if (idx >= 0) page_refcounts[idx]++;
}

void page_ref_dec(uint64_t phys) {
    int idx = phys_to_idx(phys);
    if (idx >= 0 && page_refcounts[idx] > 0) page_refcounts[idx]--;
}

uint32_t page_ref_get(uint64_t phys) {
    int idx = phys_to_idx(phys);
    if (idx < 0) return 0;
    return page_refcounts[idx];
}

// ---- Page table helpers ----

static volatile uint64_t* get_pml4_at(uint64_t pml4_phys) {
    return (volatile uint64_t*)(uint64_t)pml4_phys;
}

static int paging_walk_4kb(uint64_t pml4_phys, uint64_t virt, uint64_t** pt_entry_out, int alloc) {
    int pml4_i = (virt >> 39) & 0x1FF;
    int pdpt_i = (virt >> 30) & 0x1FF;
    int pd_i   = (virt >> 21) & 0x1FF;
    int pt_i   = (virt >> 12) & 0x1FF;

    volatile uint64_t* pml4 = get_pml4_at(pml4_phys);

    if (!(pml4[pml4_i] & PAGE_PRESENT)) {
        if (!alloc) return -1;
        uint64_t new_pdpt = (uint64_t)memory_alloc_page();
        if (!new_pdpt) return -1;
        pml4[pml4_i] = new_pdpt | PAGE_PRESENT | PAGE_RW;
    }

    volatile uint64_t* pdpt = (volatile uint64_t*)(uint64_t)(pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) {
        if (!alloc) return -1;
        uint64_t new_pd = (uint64_t)memory_alloc_page();
        if (!new_pd) return -1;
        pdpt[pdpt_i] = new_pd | PAGE_PRESENT | PAGE_RW;
    }

    volatile uint64_t* pd = (volatile uint64_t*)(uint64_t)(pdpt[pdpt_i] & ~0xFFF);
    uint64_t pd_entry = pd[pd_i];

    if (!(pd_entry & PAGE_PRESENT)) {
        if (!alloc) return -1;
        uint64_t new_pt = (uint64_t)memory_alloc_page();
        if (!new_pt) return -1;
        pd[pd_i] = new_pt | PAGE_PRESENT | PAGE_RW;
        volatile uint64_t* pt = (volatile uint64_t*)new_pt;
        *pt_entry_out = (uint64_t*)&pt[pt_i];
        return 0;
    }

    if (pd_entry & PAGE_HUGE) {
        if (!alloc) return -1;
        uint64_t huge_phys = pd_entry & ~0x1FFFFFULL;
        uint64_t huge_flags = pd_entry & 0xFFF;
        uint64_t new_pt = (uint64_t)memory_alloc_page();
        if (!new_pt) return -1;
        volatile uint64_t* pt = (volatile uint64_t*)new_pt;
        for (int i = 0; i < 512; i++) {
            pt[i] = (huge_phys + i * 4096) | (huge_flags & ~PAGE_HUGE) | PAGE_PRESENT | PAGE_RW;
        }
        pd[pd_i] = new_pt | (pd_entry & (PAGE_USER | PAGE_RW)) | PAGE_PRESENT;
        *pt_entry_out = (uint64_t*)&pt[pt_i];
        return 0;
    }

    volatile uint64_t* pt = (volatile uint64_t*)(uint64_t)(pd_entry & ~0xFFF);
    *pt_entry_out = (uint64_t*)&pt[pt_i];
    return 0;
}

int paging_map_4kb(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pte;
    if (paging_walk_4kb(pml4_phys, virt, &pte, 1) < 0)
        return -1;
    *pte = phys | flags | PAGE_PRESENT | PAGE_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void paging_unmap_4kb(uint64_t pml4_phys, uint64_t virt) {
    uint64_t* pte;
    if (paging_walk_4kb(pml4_phys, virt, &pte, 0) < 0)
        return;
    *pte = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void paging_unmap_2mb(uint64_t pml4_phys, uint64_t virt) {
    int pml4_i = (virt >> 39) & 0x1FF;
    int pdpt_i = (virt >> 30) & 0x1FF;
    int pd_i   = (virt >> 21) & 0x1FF;

    volatile uint64_t* pml4 = get_pml4_at(pml4_phys);
    if (!(pml4[pml4_i] & PAGE_PRESENT)) return;
    volatile uint64_t* pdpt = (volatile uint64_t*)(uint64_t)(pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return;
    volatile uint64_t* pd = (volatile uint64_t*)(uint64_t)(pdpt[pdpt_i] & ~0xFFF);
    pd[pd_i] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t paging_get_flags(uint64_t pml4_phys, uint64_t virt) {
    int pml4_i = (virt >> 39) & 0x1FF;
    int pdpt_i = (virt >> 30) & 0x1FF;
    int pd_i   = (virt >> 21) & 0x1FF;
    int pt_i   = (virt >> 12) & 0x1FF;

    volatile uint64_t* pml4 = get_pml4_at(pml4_phys);
    if (!(pml4[pml4_i] & PAGE_PRESENT)) return 0;
    volatile uint64_t* pdpt = (volatile uint64_t*)(uint64_t)(pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return 0;
    volatile uint64_t* pd = (volatile uint64_t*)(uint64_t)(pdpt[pdpt_i] & ~0xFFF);
    uint64_t pd_entry = pd[pd_i];
    if (!(pd_entry & PAGE_PRESENT)) return 0;
    if (pd_entry & PAGE_HUGE) return pd_entry & 0xFFF;
    volatile uint64_t* pt = (volatile uint64_t*)(uint64_t)(pd_entry & ~0xFFF);
    return pt[pt_i] & 0xFFF;
}

int paging_map_2mb(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    int pml4_i = (virt >> 39) & 0x1FF;
    int pdpt_i = (virt >> 30) & 0x1FF;
    int pd_i   = (virt >> 21) & 0x1FF;

    volatile uint64_t* pml4 = get_pml4_at(pml4_phys);
    if (!(pml4[pml4_i] & PAGE_PRESENT)) {
        uint64_t new_pdpt = (uint64_t)memory_alloc_page();
        if (!new_pdpt) return -1;
        pml4[pml4_i] = new_pdpt | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
    }

    volatile uint64_t* pdpt = (volatile uint64_t*)(uint64_t)(pml4[pml4_i] & ~0xFFF);
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) {
        uint64_t new_pd = (uint64_t)memory_alloc_page();
        if (!new_pd) return -1;
        pdpt[pdpt_i] = new_pd | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
    }

    volatile uint64_t* pd = (volatile uint64_t*)(uint64_t)(pdpt[pdpt_i] & ~0xFFF);
    pd[pd_i] = phys | PAGE_PRESENT | PAGE_RW | PAGE_HUGE | (flags & PAGE_USER);

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int memory_map_2mb(uint64_t virt, uint64_t phys, uint64_t flags) {
    return paging_map_2mb(PML4_ADDR, virt, phys, flags);
}

int memory_unmap_2mb(uint64_t virt) {
    paging_unmap_2mb(PML4_ADDR, virt);
    return 0;
}

uint64_t memory_get_flags(uint64_t virt) {
    return paging_get_flags(PML4_ADDR, virt);
}

uint64_t paging_create(void) {
    uint64_t new_pml4 = (uint64_t)memory_alloc_page();
    if (!new_pml4) return 0;

    volatile uint64_t* src_pml4 = get_pml4_at(PML4_ADDR);
    volatile uint64_t* dst_pml4 = get_pml4_at(new_pml4);

    for (int pml4_i = 0; pml4_i < 512; pml4_i++) {
        uint64_t pml4_entry = src_pml4[pml4_i];
        if (!(pml4_entry & PAGE_PRESENT)) continue;

        if (!(pml4_entry & PAGE_USER)) {
            dst_pml4[pml4_i] = pml4_entry;
            continue;
        }

        uint64_t new_pdpt = (uint64_t)memory_alloc_page();
        if (!new_pdpt) return 0;
        dst_pml4[pml4_i] = new_pdpt | (pml4_entry & (PAGE_RW | PAGE_USER | PAGE_PRESENT));

        volatile uint64_t* src_pdpt = (volatile uint64_t*)(uint64_t)(pml4_entry & ~0xFFF);
        volatile uint64_t* dst_pdpt = (volatile uint64_t*)new_pdpt;

        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            uint64_t pdpt_entry = src_pdpt[pdpt_i];
            if (!(pdpt_entry & PAGE_PRESENT)) continue;

            uint64_t new_pd = (uint64_t)memory_alloc_page();
            if (!new_pd) return 0;
            dst_pdpt[pdpt_i] = new_pd | (pdpt_entry & (PAGE_RW | PAGE_USER | PAGE_PRESENT));

            volatile uint64_t* src_pd = (volatile uint64_t*)(uint64_t)(pdpt_entry & ~0xFFF);
            volatile uint64_t* dst_pd = (volatile uint64_t*)new_pd;

            for (int pd_i = 0; pd_i < 512; pd_i++) {
                uint64_t pd_entry = src_pd[pd_i];
                if (!(pd_entry & PAGE_PRESENT)) continue;
                dst_pd[pd_i] = pd_entry;
            }
        }
    }

    return new_pml4;
}

int paging_clone_user(uint64_t dst_pml4, uint64_t src_pml4) {
    volatile uint64_t* src_pml4_v = get_pml4_at(src_pml4);
    volatile uint64_t* dst_pml4_v = get_pml4_at(dst_pml4);

    for (int pml4_i = 0; pml4_i < 512; pml4_i++) {
        uint64_t src_entry = src_pml4_v[pml4_i];
        if (!(src_entry & PAGE_PRESENT)) continue;
        if (!(src_entry & PAGE_USER)) continue;

        uint64_t dst_pml4_entry = dst_pml4_v[pml4_i];
        if (!(dst_pml4_entry & PAGE_PRESENT)) continue;

        volatile uint64_t* src_pdpt = (volatile uint64_t*)(uint64_t)(src_entry & ~0xFFF);
        volatile uint64_t* dst_pdpt = (volatile uint64_t*)(uint64_t)(dst_pml4_entry & ~0xFFF);

        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            uint64_t pdpt_entry = src_pdpt[pdpt_i];
            if (!(pdpt_entry & PAGE_PRESENT)) continue;

            uint64_t dst_pdpt_entry = dst_pdpt[pdpt_i];
            if (!(dst_pdpt_entry & PAGE_PRESENT)) continue;

            volatile uint64_t* src_pd = (volatile uint64_t*)(uint64_t)(pdpt_entry & ~0xFFF);
            volatile uint64_t* dst_pd = (volatile uint64_t*)(uint64_t)(dst_pdpt_entry & ~0xFFF);

            for (int pd_i = 0; pd_i < 512; pd_i++) {
                uint64_t src_pd_entry = src_pd[pd_i];
                if (!(src_pd_entry & PAGE_PRESENT)) continue;
                if (!(src_pd_entry & PAGE_USER)) continue;

                if (src_pd_entry & PAGE_HUGE) {
                    uint64_t huge_phys = src_pd_entry & ~0x1FFFFFULL;
                    uint64_t huge_flags = src_pd_entry & 0xFFF;
                    uint64_t pt_phys = (uint64_t)memory_alloc_page();
                    if (!pt_phys) return -1;
                    dst_pd[pd_i] = pt_phys | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);

                    volatile uint64_t* pt = (volatile uint64_t*)pt_phys;
                    for (int pt_i = 0; pt_i < 512; pt_i++) {
                        uint64_t page_phys = huge_phys + pt_i * 4096;
                        uint64_t new_page = (uint64_t)memory_alloc_page();
                        if (!new_page) return -1;
                        memory_copy((void*)new_page, (void*)page_phys, 4096);
                        pt[pt_i] = new_page | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);
                    }
                } else {
                    volatile uint64_t* src_pt = (volatile uint64_t*)(uint64_t)(src_pd_entry & ~0xFFF);
                    uint64_t pt_phys = (uint64_t)memory_alloc_page();
                    if (!pt_phys) return -1;
                    dst_pd[pd_i] = pt_phys | PAGE_PRESENT | PAGE_RW | (src_pd_entry & PAGE_USER);

                    volatile uint64_t* dst_pt = (volatile uint64_t*)pt_phys;
                    for (int pt_i = 0; pt_i < 512; pt_i++) {
                        uint64_t src_entry = src_pt[pt_i];
                        if (!(src_entry & PAGE_PRESENT)) continue;
                        uint64_t src_phys = src_entry & ~0xFFF;
                        uint64_t new_page = (uint64_t)memory_alloc_page();
                        if (!new_page) return -1;
                        memory_copy((void*)new_page, (void*)src_phys, 4096);
                        dst_pt[pt_i] = new_page | (src_entry & (PAGE_PRESENT | PAGE_RW | PAGE_USER));
                    }
                }
            }
        }
    }
    return 0;
}

void paging_destroy(uint64_t pml4_phys) {
    (void)pml4_phys;
}

void paging_switch(uint64_t pml4_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

// ---- Priority 3: Copy-on-Write (CoW) ----

static void pte_set_cow(uint64_t* pte) {
    uint64_t phys = *pte & ~0xFFFULL;
    uint64_t flags = *pte & 0xFFF;
    flags = (flags & ~(uint64_t)PAGE_RW) | PAGE_COW;
    *pte = phys | flags;
    page_ref_inc(phys);
}

int paging_cow_clone_user(uint64_t dst_pml4, uint64_t src_pml4) {
    volatile uint64_t* src_pml4_v = get_pml4_at(src_pml4);
    volatile uint64_t* dst_pml4_v = get_pml4_at(dst_pml4);

    for (int pml4_i = 0; pml4_i < 512; pml4_i++) {
        uint64_t src_entry = src_pml4_v[pml4_i];
        if (!(src_entry & PAGE_PRESENT)) continue;
        if (!(src_entry & PAGE_USER)) continue;

        uint64_t dst_pml4_entry = dst_pml4_v[pml4_i];
        if (!(dst_pml4_entry & PAGE_PRESENT)) continue;

        volatile uint64_t* src_pdpt = (volatile uint64_t*)(uint64_t)(src_entry & ~0xFFF);
        volatile uint64_t* dst_pdpt = (volatile uint64_t*)(uint64_t)(dst_pml4_entry & ~0xFFF);

        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            uint64_t pdpt_entry = src_pdpt[pdpt_i];
            if (!(pdpt_entry & PAGE_PRESENT)) continue;

            uint64_t dst_pdpt_entry = dst_pdpt[pdpt_i];
            if (!(dst_pdpt_entry & PAGE_PRESENT)) continue;

            volatile uint64_t* src_pd = (volatile uint64_t*)(uint64_t)(pdpt_entry & ~0xFFF);
            volatile uint64_t* dst_pd = (volatile uint64_t*)(uint64_t)(dst_pdpt_entry & ~0xFFF);

            for (int pd_i = 0; pd_i < 512; pd_i++) {
                uint64_t src_pd_entry = src_pd[pd_i];
                if (!(src_pd_entry & PAGE_PRESENT)) continue;
                if (!(src_pd_entry & PAGE_USER)) continue;

                if (src_pd_entry & PAGE_HUGE) {
                    uint64_t huge_phys = src_pd_entry & ~0x1FFFFFULL;
                    uint64_t huge_flags = src_pd_entry & 0xFFF;
                    uint64_t pt_phys = (uint64_t)memory_alloc_page();
                    if (!pt_phys) return -1;
                    dst_pd[pd_i] = pt_phys | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);

                    volatile uint64_t* dst_pt = (volatile uint64_t*)pt_phys;
                    uint64_t src_pt_phys = (uint64_t)memory_alloc_page();
                    if (!src_pt_phys) return -1;
                    src_pd[pd_i] = src_pt_phys | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);

                    volatile uint64_t* src_pt = (volatile uint64_t*)src_pt_phys;
                    for (int pt_i = 0; pt_i < 512; pt_i++) {
                        uint64_t page_phys = huge_phys + pt_i * 4096;
                        uint64_t cow_flags = (huge_flags & ~PAGE_HUGE) | PAGE_PRESENT | PAGE_COW;
                        cow_flags = cow_flags & ~(uint64_t)PAGE_RW;
                        src_pt[pt_i] = page_phys | cow_flags;
                        dst_pt[pt_i] = page_phys | cow_flags;
                        page_ref_inc(page_phys);
                        page_ref_inc(page_phys);
                    }
                } else {
                    volatile uint64_t* src_pt = (volatile uint64_t*)(uint64_t)(src_pd_entry & ~0xFFF);
                    uint64_t dst_pt_phys = (uint64_t)memory_alloc_page();
                    if (!dst_pt_phys) return -1;
                    dst_pd[pd_i] = dst_pt_phys | PAGE_PRESENT | PAGE_RW | (src_pd_entry & PAGE_USER);

                    volatile uint64_t* dst_pt = (volatile uint64_t*)dst_pt_phys;
                    for (int pt_i = 0; pt_i < 512; pt_i++) {
                        uint64_t src_pte = src_pt[pt_i];
                        if (!(src_pte & PAGE_PRESENT)) {
                            dst_pt[pt_i] = 0;
                            continue;
                        }
                        uint64_t phys = src_pte & ~0xFFFULL;

                        pte_set_cow((uint64_t*)&src_pt[pt_i]);
                        uint64_t cow_flags = src_pt[pt_i] & 0xFFF;
                        dst_pt[pt_i] = phys | cow_flags;
                        page_ref_inc(phys);
                    }
                }
            }
        }
    }
    return 0;
}

int paging_handle_cow_fault(uint64_t pml4, uint64_t fault_addr, uint64_t err_code) {
    (void)err_code;
    uint64_t* pte;
    if (paging_walk_4kb(pml4, fault_addr, &pte, 0) < 0)
        return -1;

    if (!(*pte & PAGE_PRESENT))
        return -1;

    if (!(*pte & PAGE_COW))
        return -1;

    uint64_t phys = *pte & ~0xFFFULL;
    uint64_t flags = *pte & 0xFFF;
    flags = (flags & ~(uint64_t)PAGE_COW) | PAGE_RW;

    if (page_ref_get(phys) > 1) {
        uint64_t new_phys = (uint64_t)memory_alloc_page();
        if (!new_phys) return -1;
        memory_copy((void*)new_phys, (void*)phys, 4096);
        page_ref_dec(phys);
        *pte = new_phys | flags | PAGE_PRESENT;
    } else {
        *pte = phys | flags | PAGE_PRESENT;
        page_ref_dec(phys);
    }

    __asm__ volatile("invlpg (%0)" : : "r"(fault_addr) : "memory");
    return 0;
}

// ---- Priority 3: Demand Paging ----

void vm_region_init(vm_region_t* regions) {
    for (int i = 0; i < MAX_VM_REGIONS; i++) {
        regions[i].start = 0;
        regions[i].end = 0;
        regions[i].flags = 0;
    }
}

int vm_region_add(vm_region_t* regions, uint64_t start, uint64_t end, uint64_t flags) {
    for (int i = 0; i < MAX_VM_REGIONS; i++) {
        if (regions[i].start == 0 && regions[i].end == 0) {
            regions[i].start = start;
            regions[i].end = end;
            regions[i].flags = flags;
            return 0;
        }
    }
    return -1;
}

int vm_region_contains(vm_region_t* regions, uint64_t addr, uint64_t* flags_out) {
    for (int i = 0; i < MAX_VM_REGIONS; i++) {
        if (regions[i].start == 0 && regions[i].end == 0)
            continue;
        if (addr >= regions[i].start && addr < regions[i].end) {
            if (flags_out) *flags_out = regions[i].flags;
            return 1;
        }
    }
    return 0;
}

int vm_region_copy(vm_region_t* dst, vm_region_t* src) {
    for (int i = 0; i < MAX_VM_REGIONS; i++) {
        dst[i].start = src[i].start;
        dst[i].end = src[i].end;
        dst[i].flags = src[i].flags;
    }
    return 0;
}

int paging_handle_demand_fault(uint64_t pml4, uint64_t fault_addr, uint64_t err_code, vm_region_t* regions) {
    (void)err_code;
    if (!regions) return -1;

    uint64_t page_addr = fault_addr & ~0xFFFULL;
    uint64_t flags;
    if (!vm_region_contains(regions, fault_addr, &flags))
        return -1;

    uint64_t* pte;
    int walk_result = paging_walk_4kb(pml4, page_addr, &pte, 1);
    if (walk_result < 0) return -1;

    if (*pte & PAGE_PRESENT)
        return 0;

    uint64_t new_phys = (uint64_t)memory_alloc_page();
    if (!new_phys) return -1;

    *pte = new_phys | flags | PAGE_PRESENT;
    __asm__ volatile("invlpg (%0)" : : "r"(page_addr) : "memory");
    return 0;
}

// ---- Priority 3: Spinlocks ----

void spinlock_init(spinlock_t* sl) {
    sl->lock = 0;
}

void spinlock_acquire(spinlock_t* sl) {
    while (__sync_lock_test_and_set(&sl->lock, 1)) {
        __asm__ volatile("pause");
    }
}

void spinlock_release(spinlock_t* sl) {
    __sync_lock_release(&sl->lock);
}

int spinlock_try_acquire(spinlock_t* sl) {
    return __sync_lock_test_and_set(&sl->lock, 1) == 0;
}

void memory_print_pagetables(void) {
    volatile uint64_t* pml4 = (volatile uint64_t*)PML4_ADDR;
    serial_write("PML4:\n", 6);
    for (int i = 0; i < 512; i++) {
        if (pml4[i] & PAGE_PRESENT) {
            serial_write("  [", 3);
            serial_putchar('0' + (i / 100));
            serial_putchar('0' + ((i / 10) % 10));
            serial_putchar('0' + (i % 10));
            serial_write("] -> 0x", 7);
            uint64_t addr = pml4[i] & ~0xFFF;
            for (int shift = 36; shift >= 0; shift -= 4) {
                char hex = (addr >> shift) & 0xF;
                serial_putchar(hex < 10 ? '0' + hex : 'a' + hex - 10);
            }
            serial_write(" flags:", 7);
            if (pml4[i] & PAGE_USER) serial_write(" USER", 5);
            if (pml4[i] & PAGE_RW) serial_write(" RW", 3);
            serial_putchar('\n');
        }
    }
}
