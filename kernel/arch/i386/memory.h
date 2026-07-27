#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT  0x001
#define PAGE_RW       0x002
#define PAGE_USER     0x004
#define PAGE_ACCESSED 0x040
#define PAGE_DIRTY    0x080
#define PAGE_HUGE     0x080
#define PAGE_GLOBAL   0x100
#define PAGE_COW      0x200

#define PD_ADDR       0x1000
#define PT_ADDR       0x2000

#define PAGE_SIZE_4KB 0x1000
#define PAGE_SIZE_4MB 0x400000

#define MAX_VM_REGIONS 8

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
} vm_region_t;

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT {0}

void memory_init(void* heap_start, size_t heap_size);
void* memory_alloc(size_t size);
void memory_free(void* ptr);
void* memory_alloc_page(void);
void* memory_copy(void* dest, const void* src, size_t n);
void memory_set(void* dest, int val, size_t n);

int paging_map_4kb(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags);
void paging_unmap_4kb(uint32_t pd_phys, uint32_t virt);
uint32_t paging_get_flags(uint32_t pd_phys, uint32_t virt);
int paging_map_4mb(uint32_t pd_phys, uint32_t virt, uint32_t phys, uint32_t flags);

uint32_t paging_create(void);
int paging_clone_user(uint32_t dst_pd, uint32_t src_pd);
void paging_destroy(uint32_t pd_phys);
void paging_switch(uint32_t pd_phys);

void page_ref_init(void);
void page_ref_inc(uint32_t phys);
void page_ref_dec(uint32_t phys);
uint32_t page_ref_get(uint32_t phys);

int paging_cow_clone_user(uint32_t dst_pd, uint32_t src_pd);
int paging_handle_cow_fault(uint32_t pd, uint32_t fault_addr, uint32_t err_code);

void vm_region_init(vm_region_t* regions);
int vm_region_add(vm_region_t* regions, uint32_t start, uint32_t end, uint32_t flags);
int vm_region_contains(vm_region_t* regions, uint32_t addr, uint32_t* flags_out);
int vm_region_copy(vm_region_t* dst, vm_region_t* src);

int paging_handle_demand_fault(uint32_t pd, uint32_t fault_addr, uint32_t err_code, vm_region_t* regions);

void spinlock_init(spinlock_t* sl);
void spinlock_acquire(spinlock_t* sl);
void spinlock_release(spinlock_t* sl);
int spinlock_try_acquire(spinlock_t* sl);

#endif
