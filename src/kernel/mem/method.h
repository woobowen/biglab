#pragma once
#include "lib/type.h"
#include "type.h"      // <--- 修复: 包含 type.h (定义 pagetable_t)
#include "proc/type.h" // <--- 修复: 包含 proc/type.h (定义 proc_t)

/* pmem.c: 物理内存管理逻辑 */

void pmem_init(void);
void *pmem_alloc(bool in_kernel);
void pmem_free(uint64 page, bool in_kernel);


/* kvm.c: 内核态虚拟内存管理 + 页表通用函数 */

/* kvm.c: 内核态虚拟内存管理 + 页表通用函数 */

pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);
void vm_print(pgtbl_t pgtbl);
void kvm_init();
void kvm_inithart();
void kvm_map_trampoline(pgtbl_t pgtbl);


/* uvm.c: 用户态虚拟内存管理 */

// --- Lab 5 的函数原型 ---
// --- Lab 5 的函数原型 ---
int uvm_copyin(pgtbl_t pgtbl, char *dst, uint64 src, uint64 len);
int uvm_copyout(pgtbl_t pgtbl, uint64 dst, char *src, uint64 len);
int uvm_copyin_str(pgtbl_t pgtbl, char *dst, uint64 src, uint64 max_len);
void uvm_show_mmaplist(mmap_region_t *mmap);
uint64 uvm_mmap(proc_t *p, uint64 begin, uint64 len, int prot);
int uvm_munmap(proc_t *p, uint64 begin, uint64 len);
int uvm_heap_grow(proc_t *p, uint64 new_top);
int uvm_heap_ungrow(proc_t *p, uint64 new_top);
int uvm_ustack_grow(proc_t *p, uint64 fault_va);
void uvm_destroy_pgtbl(proc_t *p);
int uvm_copy_pgtbl(proc_t *p_old, proc_t *p_new);
// ---------------------------------


/* mmap.c: mmap_node仓库管理 */

void mmap_init();
mmap_region_t *mmap_region_alloc();
void mmap_region_free(mmap_region_t *mmap);
void mmap_show_nodelist();