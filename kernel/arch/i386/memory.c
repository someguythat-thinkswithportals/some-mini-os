#include "memory.h"
#include "serial.h"

typedef struct block {
    size_t size;
    int free;
    struct block* next;
} block_t;

static block_t* heap_start_block;

void memory_page_init(void);

void memory_init(void* heap_start_ptr, size_t heap_size) {
    heap_start_block = (block_t*)heap_start_ptr;
    heap_start_block->size = heap_size - sizeof(block_t);
    heap_start_block->free = 1;
    heap_start_block->next = NULL;
    memory_page_init();
    page_ref_init();
}

void* memory_alloc(size_t size) {
    block_t* curr = heap_start_block;
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
    block_t* curr = heap_start_block;
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

static uint32_t page_alloc_cur;
static uint32_t page_alloc_end;

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


#define PAGE_ALLOC_START 0x600000
#define PAGE_ALLOC_END   0x800000
#define MAX_PHYS_PAGES   ((PAGE_ALLOC_END - PAGE_ALLOC_START) / 4096)

static uint32_t page_refcounts[MAX_PHYS_PAGES];

static int phys_to_idx(uint32_t phys) {
    if (phys < PAGE_ALLOC_START || phys >= PAGE_ALLOC_END) return -1;
    return (int)((phys - PAGE_ALLOC_START) / 4096);
}

void page_ref_init(void) {
    for (int i = 0; i < MAX_PHYS_PAGES; i++)
        page_refcounts[i] = 0;
}

void page_ref_inc(uint32_t phys) {
    int idx = phys_to_idx(phys);
    if (idx >= 0) page_refcounts[idx]++;
}

void page_ref_dec(uint32_t phys) {
    int idx = phys_to_idx(phys);
    if (idx >= 0 && page_refcounts[idx] > 0) page_refcounts[idx]--;
}

uint32_t page_ref_get(uint32_t phys) {
    int idx = phys_to_idx(phys);
    if (idx < 0) return 0;
    return page_refcounts[idx];
}

// ---- Page table helpers (2-level: PD → PT) ----

static volatile uint32_t* get_pd_at(uint32_t pd_phys) {
    return (volatile uint32_t*)pd_phys;
}

static int paging_walk_4kb(uint32_t pd_phys, uint32_t virt, uint32_t** pt_entry_out, int alloc) {
    int pd_i = (virt >> 22) & 0x3FF;
    int pt_i = (virt >> 12) & 0x3FF;

    volatile uint32_t* pd = get_pd_at(pd_phys);

    if (!(pd[pd_i] & PAGE_PRESENT)) {
        if (!alloc) return -1;
        uint32_t new_pt = (uint32_t)memory_alloc_page();
        if (!new_pt) return -1;
        pd[pd_i] = new_pt | PAGE_PRESENT | PAGE_RW;
    }

    if (pd[pd_i] & PAGE_HUGE) {
        if (!alloc) return -1;
        uint32_t huge_phys = pd[pd_i] & ~0x3FFFFF;
        uint32_t huge_flags = pd[pd_i] & 0xFFF;
        uint32_t new_pt = (uint32_t)memory_alloc_page();
        if (!new_pt) return -1;
        volatile uint32_t* pt = (volatile uint32_t*)new_pt;
        for (int i = 0; i < 1024; i++) {
            pt[i] = (huge_phys + i * 4096) | (huge_flags & ~PAGE_HUGE) | PAGE_PRESENT | PAGE_RW;
        }
        pd[pd_i] = new_pt | PAGE_USER | PAGE_RW | PAGE_PRESENT;
        *pt_entry_out = (uint32_t*)&pt[pt_i];
        return 0;
    }

    volatile uint32_t* pt = (volatile uint32_t*)(pd[pd_i] & ~0xFFF);
    *pt_entry_out = (uint32_t*)&pt[pt_i];
    return 0;
}

int paging_map_4kb(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t* pte;
    if (paging_walk_4kb(pd_phys, virt, &pte, 1) < 0)
        return -1;
    *pte = phys | flags | PAGE_PRESENT | PAGE_RW;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void paging_unmap_4kb(uint32_t pd_phys, uint32_t virt) {
    uint32_t* pte;
    if (paging_walk_4kb(pd_phys, virt, &pte, 0) < 0)
        return;
    *pte = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint32_t paging_get_flags(uint32_t pd_phys, uint32_t virt) {
    int pd_i = (virt >> 22) & 0x3FF;
    int pt_i = (virt >> 12) & 0x3FF;

    volatile uint32_t* pd = get_pd_at(pd_phys);
    if (!(pd[pd_i] & PAGE_PRESENT)) return 0;

    if (pd[pd_i] & PAGE_HUGE) return pd[pd_i] & 0xFFF;

    volatile uint32_t* pt = (volatile uint32_t*)(pd[pd_i] & ~0xFFF);
    return pt[pt_i] & 0xFFF;
}

int paging_map_4mb(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags) {
    int pd_i = (virt >> 22) & 0x3FF;

    volatile uint32_t* pd = get_pd_at(pd_phys);
    pd[pd_i] = (phys & ~0x3FFFFF) | PAGE_PRESENT | PAGE_RW | PAGE_HUGE | (flags & PAGE_USER);

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

uint32_t paging_create(void) {
    uint32_t new_pd = (uint32_t)memory_alloc_page();
    if (!new_pd) return 0;

    volatile uint32_t* src_pd = get_pd_at(PD_ADDR);
    volatile uint32_t* dst_pd = get_pd_at(new_pd);

    for (int pd_i = 0; pd_i < 1024; pd_i++) {
        uint32_t src_entry = src_pd[pd_i];
        if (!(src_entry & PAGE_PRESENT)) continue;

        if (!(src_entry & PAGE_USER)) {
            dst_pd[pd_i] = src_entry;
            continue;
        }

        uint32_t new_pt = (uint32_t)memory_alloc_page();
        if (!new_pt) return 0;
        dst_pd[pd_i] = new_pt | (src_entry & (PAGE_RW | PAGE_USER | PAGE_PRESENT));

        volatile uint32_t* src_pt = (volatile uint32_t*)(src_entry & ~0xFFF);
        volatile uint32_t* dst_pt = (volatile uint32_t*)new_pt;

        for (int pt_i = 0; pt_i < 1024; pt_i++) {
            dst_pt[pt_i] = src_pt[pt_i];
        }
    }

    return new_pd;
}

int paging_clone_user(uint32_t dst_pd, uint32_t src_pd_phys) {
    volatile uint32_t* src_pd = get_pd_at(src_pd_phys);
    volatile uint32_t* dst_pd_v = get_pd_at(dst_pd);

    for (int pd_i = 0; pd_i < 1024; pd_i++) {
        uint32_t src_entry = src_pd[pd_i];
        if (!(src_entry & PAGE_PRESENT)) continue;
        if (!(src_entry & PAGE_USER)) continue;

        uint32_t dst_entry = dst_pd_v[pd_i];
        if (!(dst_entry & PAGE_PRESENT)) continue;

        if (src_entry & PAGE_HUGE) {
            uint32_t huge_phys = src_entry & ~0x3FFFFF;
            uint32_t huge_flags = src_entry & 0xFFF;
            uint32_t pt_phys = (uint32_t)memory_alloc_page();
            if (!pt_phys) return -1;
            dst_pd_v[pd_i] = pt_phys | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);

            volatile uint32_t* pt = (volatile uint32_t*)pt_phys;
            for (int pt_i = 0; pt_i < 1024; pt_i++) {
                uint32_t page_phys = huge_phys + pt_i * 4096;
                uint32_t new_page = (uint32_t)memory_alloc_page();
                if (!new_page) return -1;
                memory_copy((void*)new_page, (void*)page_phys, 4096);
                pt[pt_i] = new_page | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);
            }
        } else {
            volatile uint32_t* src_pt = (volatile uint32_t*)(src_entry & ~0xFFF);
            uint32_t pt_phys = (uint32_t)memory_alloc_page();
            if (!pt_phys) return -1;
            dst_pd_v[pd_i] = pt_phys | PAGE_PRESENT | PAGE_RW | (src_entry & PAGE_USER);

            volatile uint32_t* dst_pt = (volatile uint32_t*)pt_phys;
            for (int pt_i = 0; pt_i < 1024; pt_i++) {
                uint32_t src_pte = src_pt[pt_i];
                if (!(src_pte & PAGE_PRESENT)) continue;
                uint32_t src_phys = src_pte & ~0xFFF;
                uint32_t new_page = (uint32_t)memory_alloc_page();
                if (!new_page) return -1;
                memory_copy((void*)new_page, (void*)src_phys, 4096);
                dst_pt[pt_i] = new_page | (src_pte & (PAGE_PRESENT | PAGE_RW | PAGE_USER));
            }
        }
    }
    return 0;
}

void paging_destroy(uint32_t pd_phys) {
    (void)pd_phys;
}

void paging_switch(uint32_t pd_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
}


static void pte_set_cow(uint32_t* pte) {
    uint32_t phys = *pte & ~0xFFF;
    uint32_t flags = *pte & 0xFFF;
    flags = (flags & ~PAGE_RW) | PAGE_COW;
    *pte = phys | flags;
    page_ref_inc(phys);
}

int paging_cow_clone_user(uint32_t dst_pd, uint32_t src_pd_phys) {
    volatile uint32_t* src_pd = get_pd_at(src_pd_phys);
    volatile uint32_t* dst_pd_v = get_pd_at(dst_pd);

    for (int pd_i = 0; pd_i < 1024; pd_i++) {
        uint32_t src_entry = src_pd[pd_i];
        if (!(src_entry & PAGE_PRESENT)) continue;
        if (!(src_entry & PAGE_USER)) continue;

        uint32_t dst_entry = dst_pd_v[pd_i];
        if (!(dst_entry & PAGE_PRESENT)) continue;

        if (src_entry & PAGE_HUGE) {
            uint32_t huge_phys = src_entry & ~0x3FFFFF;
            uint32_t huge_flags = src_entry & 0xFFF;
            uint32_t dst_pt_phys = (uint32_t)memory_alloc_page();
            if (!dst_pt_phys) return -1;
            dst_pd_v[pd_i] = dst_pt_phys | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);

            volatile uint32_t* dst_pt = (volatile uint32_t*)dst_pt_phys;
            uint32_t src_pt_phys = (uint32_t)memory_alloc_page();
            if (!src_pt_phys) return -1;
            src_pd[pd_i] = src_pt_phys | PAGE_PRESENT | PAGE_RW | (huge_flags & PAGE_USER);

            volatile uint32_t* src_pt = (volatile uint32_t*)src_pt_phys;
            for (int pt_i = 0; pt_i < 1024; pt_i++) {
                uint32_t page_phys = huge_phys + pt_i * 4096;
                uint32_t cow_flags = (huge_flags & ~PAGE_HUGE) | PAGE_PRESENT | PAGE_COW;
                cow_flags = cow_flags & ~PAGE_RW;
                src_pt[pt_i] = page_phys | cow_flags;
                dst_pt[pt_i] = page_phys | cow_flags;
                page_ref_inc(page_phys);
                page_ref_inc(page_phys);
            }
        } else {
            volatile uint32_t* src_pt = (volatile uint32_t*)(src_entry & ~0xFFF);
            uint32_t dst_pt_phys = (uint32_t)memory_alloc_page();
            if (!dst_pt_phys) return -1;
            dst_pd_v[pd_i] = dst_pt_phys | PAGE_PRESENT | PAGE_RW | (src_entry & PAGE_USER);

            volatile uint32_t* dst_pt = (volatile uint32_t*)dst_pt_phys;
            for (int pt_i = 0; pt_i < 1024; pt_i++) {
                uint32_t src_pte = src_pt[pt_i];
                if (!(src_pte & PAGE_PRESENT)) {
                    dst_pt[pt_i] = 0;
                    continue;
                }
                uint32_t phys = src_pte & ~0xFFF;

                pte_set_cow((uint32_t*)&src_pt[pt_i]);
                uint32_t cow_flags = src_pt[pt_i] & 0xFFF;
                dst_pt[pt_i] = phys | cow_flags;
                page_ref_inc(phys);
            }
        }
    }
    return 0;
}

int paging_handle_cow_fault(uint32_t pd, uint32_t fault_addr, uint32_t err_code) {
    (void)err_code;
    uint32_t* pte;
    if (paging_walk_4kb(pd, fault_addr, &pte, 0) < 0)
        return -1;

    if (!(*pte & PAGE_PRESENT))
        return -1;

    if (!(*pte & PAGE_COW))
        return -1;

    uint32_t phys = *pte & ~0xFFF;
    uint32_t flags = *pte & 0xFFF;
    flags = (flags & ~PAGE_COW) | PAGE_RW;

    if (page_ref_get(phys) > 1) {
        uint32_t new_phys = (uint32_t)memory_alloc_page();
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


void vm_region_init(vm_region_t* regions) {
    for (int i = 0; i < MAX_VM_REGIONS; i++) {
        regions[i].start = 0;
        regions[i].end = 0;
        regions[i].flags = 0;
    }
}

int vm_region_add(vm_region_t* regions, uint32_t start, uint32_t end, uint32_t flags) {
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

int vm_region_contains(vm_region_t* regions, uint32_t addr, uint32_t* flags_out) {
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

int paging_handle_demand_fault(uint32_t pd, uint32_t fault_addr, uint32_t err_code, vm_region_t* regions) {
    (void)err_code;
    if (!regions) return -1;

    uint32_t page_addr = fault_addr & ~0xFFF;
    uint32_t flags;
    if (!vm_region_contains(regions, fault_addr, &flags))
        return -1;

    uint32_t* pte;
    int walk_result = paging_walk_4kb(pd, page_addr, &pte, 1);
    if (walk_result < 0) return -1;

    if (*pte & PAGE_PRESENT) {
        uint32_t need = flags | PAGE_PRESENT;
        if ((*pte & need) != need) {
            *pte |= need;
            __asm__ volatile("invlpg (%0)" : : "r"(page_addr) : "memory");
        }
        return 0;
    }

    uint32_t new_phys = (uint32_t)memory_alloc_page();
    if (!new_phys) return -1;

    *pte = new_phys | flags | PAGE_PRESENT;
    __asm__ volatile("invlpg (%0)" : : "r"(page_addr) : "memory");
    return 0;
}


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
