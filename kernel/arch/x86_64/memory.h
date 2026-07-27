#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT  0x001
#define PAGE_RW       0x002
#define PAGE_USER     0x004
#define PAGE_WRITETH  0x008
#define PAGE_PWT      0x010
#define PAGE_CD       0x020
#define PAGE_ACCESSED 0x040
#define PAGE_DIRTY    0x080
#define PAGE_HUGE     0x080
#define PAGE_GLOBAL   0x100
#define PAGE_COW      0x200

#define PML4_ADDR     0x1000
#define PDPT_ADDR     0x2000
#define PD_ADDR       0x3000

#define PAGE_SIZE_4KB 0x1000
#define PAGE_SIZE_2MB 0x200000

void memory_init(void* heap_start, size_t heap_size);
void* memory_alloc(size_t size);
void memory_free(void* ptr);
void* memory_alloc_page(void);
void* memory_copy(void* dest, const void* src, size_t n);
void memory_set(void* dest, int val, size_t n);

int memory_map_2mb(uint64_t virt, uint64_t phys, uint64_t flags);
int memory_unmap_2mb(uint64_t virt);
uint64_t memory_get_flags(uint64_t virt);
void memory_print_pagetables(void);

int paging_map_4kb(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);
void paging_unmap_4kb(uint64_t pml4_phys, uint64_t virt);
void paging_unmap_2mb(uint64_t pml4_phys, uint64_t virt);
uint64_t paging_get_flags(uint64_t pml4_phys, uint64_t virt);
int paging_map_2mb(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t flags);

uint64_t paging_create(void);
int paging_clone_user(uint64_t dst_pml4, uint64_t src_pml4);
void paging_destroy(uint64_t pml4_phys);
void paging_switch(uint64_t pml4_phys);

// --- Priority 3: Physical page reference counting ---
void page_ref_init(void);
void page_ref_inc(uint64_t phys);
void page_ref_dec(uint64_t phys);
uint32_t page_ref_get(uint64_t phys);

// --- Priority 3: Copy-on-Write (CoW) ---
int paging_cow_clone_user(uint64_t dst_pml4, uint64_t src_pml4);
int paging_handle_cow_fault(uint64_t pml4, uint64_t fault_addr, uint64_t err_code);

// --- Priority 3: Demand paging ---
#define MAX_VM_REGIONS 8

typedef struct {
    uint64_t start;
    uint64_t end;
    uint64_t flags;
} vm_region_t;

void vm_region_init(vm_region_t* regions);
int vm_region_add(vm_region_t* regions, uint64_t start, uint64_t end, uint64_t flags);
int vm_region_contains(vm_region_t* regions, uint64_t addr, uint64_t* flags_out);
int vm_region_copy(vm_region_t* dst, vm_region_t* src);

int paging_handle_demand_fault(uint64_t pml4, uint64_t fault_addr, uint64_t err_code, vm_region_t* regions);

// --- Priority 3: Spinlocks & Synchronization ---
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT {0}

void spinlock_init(spinlock_t* sl);
void spinlock_acquire(spinlock_t* sl);
void spinlock_release(spinlock_t* sl);
int spinlock_try_acquire(spinlock_t* sl);

#endif
