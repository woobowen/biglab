#include "lib/type.h"
#include "lib/mod.h"
#include "lib/method.h"  // <--- 包含 printf, memset 等

#include "mem/type.h"    // <--- 包含 pgtbl_t, UHEAP_START, PTE_TO_PA, PGROUNDUP 等
#include "mem/method.h"  // <--- 包含 pmem_alloc, vm_getpte, vm_mappages, mmap_region_alloc 等

#include "proc/type.h"   // <--- 包含 proc_t
#include "proc/method.h" // <--- 包含 myproc


/*
 * ---------------- uvm_copyin / uvm_copyout ----------------
 */

int uvm_copyin(pgtbl_t pgtbl, char *dst, uint64 src, uint64 len)
{
    uint64 n, va, pa;
    pte_t *pte;

    for (n = len; n > 0; n -= PGSIZE, src += PGSIZE, dst += PGSIZE) {
        uint64 offset = src & (PGSIZE - 1);
        uint64 copy_len = (n < (PGSIZE - offset)) ? n : (PGSIZE - offset);

        va = src & (~(PGSIZE - 1));
        pte = vm_getpte(pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
        if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1; // 页不存在或无用户权限
        
        pa = PTE_TO_PA(*pte); // <--- 修复: PTE2PA -> PTE_TO_PA
        memmove(dst, (void *)(pa + offset), copy_len);
    }
    return 0;
}

int uvm_copyout(pgtbl_t pgtbl, uint64 dst, char *src, uint64 len)
{
    uint64 n, va, pa;
    pte_t *pte;

    for (n = len; n > 0; n -= PGSIZE, dst += PGSIZE, src += PGSIZE) {
        uint64 offset = dst & (PGSIZE - 1);
        uint64 copy_len = (n < (PGSIZE - offset)) ? n : (PGSIZE - offset);
        
        va = dst & (~(PGSIZE - 1));
        pte = vm_getpte(pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
        if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
            return -1; // 页不存在, 或无用户/写权限
        
        pa = PTE_TO_PA(*pte); // <--- 修复: PTE2PA -> PTE_TO_PA
        memmove((void *)(pa + offset), src, copy_len);
    }
    return 0;
}

int uvm_copyin_str(pgtbl_t pgtbl, char *dst, uint64 src, uint64 max_len)
{
    uint64 va, pa, offset;
    int count = 0;
    pte_t *pte;

    for (; count < max_len; count++, src++, dst++) {
        offset = src & (PGSIZE - 1);
        va = src & (~(PGSIZE - 1));
        
        pte = vm_getpte(pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
        if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1; // 页不存在或无用户权限
        
        pa = PTE_TO_PA(*pte); // <--- 修复: PTE2PA -> PTE_TO_PA
        *dst = *(char *)(pa + offset);
        if (*dst == '\0') {
            return count + 1;
        }
    }
    return -1; // 未找到 '\0' 且达到了 max_len
}


/*
 * ---------------- uvm_heap ----------------
 */

int uvm_heap_grow(proc_t *p, uint64 new_top)
{
    uint64 old_top = p->heap_top;
    uint64 old_top_pg = PGROUNDUP(old_top); // <--- 依赖 mem/type.h
    uint64 new_top_pg = PGROUNDUP(new_top);

    if (new_top_pg <= old_top_pg) {
        p->heap_top = new_top;
        return 0;
    }

    if (new_top_pg > MMAP_BEGIN) { // <--- 依赖 mem/type.h
        return -1; // 超过 mmap 区域
    }

    // 必须逐页分配和映射
    uint64 va;
    for (va = old_top_pg; va < new_top_pg; va += PGSIZE) {
        void *pa = pmem_alloc(false); // (false) 为用户空间分配物理页
        if (pa == 0) {
            // 分配失败, 需要回滚
            vm_unmappages(p->pgtbl, old_top_pg, va - old_top_pg, true); // (true) 释放刚分配的页
            return -1;
        }
        
        // --- 修复: 移除 if 检查 ---
        // (true) 映射刚分配的页
        vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE, PTE_U | PTE_R | PTE_W);
    }

    p->heap_top = new_top;
    return 0;
}

int uvm_heap_ungrow(proc_t *p, uint64 new_top)
{
    uint64 old_top = p->heap_top;
    uint64 old_top_pg = PGROUNDUP(old_top);
    uint64 new_top_pg = PGROUNDUP(new_top);

    if (new_top_pg < old_top_pg) {
        vm_unmappages(p->pgtbl, new_top_pg, old_top_pg - new_top_pg, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
    }

    p->heap_top = new_top;
    return 0;
}


/*
 * ---------------- uvm_stack ----------------
 */

int uvm_ustack_grow(proc_t *p, uint64 fault_va)
{
    // uint64 pgtbl = (uint64)p->pgtbl; // <--- 修复: 移除未使用的变量
    uint64 ustack_top = VA_MAX - (p->ustack_npage + 2) * PGSIZE;

    // 检查 fault_va 是否是合法的栈扩展地址
    if (fault_va >= ustack_top || fault_va < MMAP_END) {
        return -1; // 非法地址
    }

    // 计算新的栈底页 (RISC-V 栈向下增长)
    uint64 new_page_va = PGROUNDDOWN(fault_va); // <--- 依赖 mem/type.h

    if (new_page_va < ustack_top - PGSIZE) {
        // 需要一次性扩展多页
        uint64 va;
        for (va = ustack_top - PGSIZE; va >= new_page_va; va -= PGSIZE) {
            // --- 修复: uvm_map_pages -> pmem_alloc + vm_mappages ---
            void *pa = pmem_alloc(false); // (false) 分配用户物理页
            if (pa == 0) {
                // 分配失败, 回滚
                vm_unmappages(p->pgtbl, va + PGSIZE, ustack_top - (va + PGSIZE), true); // (true) 释放刚分配的页
                return -1;
            }
            
            // --- 修复: 移除 if 检查 ---
            vm_mappages(p->pgtbl, va, (uint64)pa, PGSIZE, PTE_U | PTE_R | PTE_W);
        }
    } else {
         // 扩展一页
         // --- 修复: uvm_map_pages -> pmem_alloc + vm_mappages ---
        void *pa = pmem_alloc(false); // (false) 分配用户物理页
        if (pa == 0) {
            return -1; // 失败
        }
        
        // --- 修复: 移除 if 检查 ---
        vm_mappages(p->pgtbl, new_page_va, (uint64)pa, PGSIZE, PTE_U | PTE_R | PTE_W);
    }

    // 更新栈占用的页数
    p->ustack_npage = (VA_MAX - 2 * PGSIZE - new_page_va) / PGSIZE;
    
    return 0;
}


/*
 * ---------------- uvm_mmap ----------------
 */

// 辅助函数: 合并相邻的 mmap_region
void mmap_merge(mmap_region_t *pre, mmap_region_t *cur)
{
    mmap_region_t *next = pre->next;

    // 尝试与 pre 合并
    if (pre->begin + pre->npages * PGSIZE == cur->begin) {
        pre->npages += cur->npages;
        mmap_region_free(cur); // 释放 cur 节点
        cur = pre; // cur 指向合并后的 pre
    }

    // 尝试与 next 合并
    if (next) {
        if (cur->begin + cur->npages * PGSIZE == next->begin) {
            cur->npages += next->npages;
            cur->next = next->next;
            mmap_region_free(next); // 释放 next 节点
        }
    }

    // 如果 pre 和 cur 没有合并, 正常插入
    if (cur != pre) {
        cur->next = pre->next;
        pre->next = cur;
    }
}


uint64 uvm_mmap(proc_t *p, uint64 begin, uint64 len, int prot)
{
    uint32 npages = PGROUNDUP(len) / PGSIZE;
    if (npages == 0) return -1;

    mmap_region_t *new_node = mmap_region_alloc(); // <--- 依赖 mem/method.h
    if (new_node == 0) return -1;

    new_node->npages = npages;

    // begin == 0: 寻找可用空间
    if (begin == 0) {
        uint64 search_begin = MMAP_BEGIN;
        mmap_region_t *cur = p->mmap;
        while (cur) {
            if (search_begin + npages * PGSIZE <= cur->begin) {
                // 找到了
                begin = search_begin;
                break;
            }
            search_begin = cur->begin + cur->npages * PGSIZE;
            cur = cur->next;
        }
        if (begin == 0) { // 循环结束仍未找到
            if (search_begin + npages * PGSIZE <= MMAP_END) {
                begin = search_begin; // 链表尾部
            } else {
                mmap_region_free(new_node); // 空间不足
                return -1;
            }
        }
    } 
    // begin != 0: 检查重叠
    else {
        mmap_region_t *cur = p->mmap;
        while (cur) {
            uint64 cur_end = cur->begin + cur->npages * PGSIZE;
            uint64 req_end = begin + npages * PGSIZE;
            // 检查重叠: (req_begin < cur_end) && (req_end > cur_begin)
            if (begin < cur_end && req_end > cur->begin) {
                mmap_region_free(new_node); // 重叠
                return -1;
            }
            cur = cur->next;
        }
    }

    // 检查是否越界
    if (begin < MMAP_BEGIN || (begin + npages * PGSIZE) > MMAP_END) {
        mmap_region_free(new_node);
        return -1;
    }

    new_node->begin = begin;

    // 插入并合并
    // 创建一个虚拟头节点, 简化边界处理
    mmap_region_t dummy_head;
    dummy_head.begin = 0;
    dummy_head.npages = 0;
    dummy_head.next = p->mmap;

    mmap_region_t *pre = &dummy_head;
    while (pre->next && pre->next->begin < begin) {
        pre = pre->next;
    }

    // pre 是新节点的前一个节点
    mmap_merge(pre, new_node);
    p->mmap = dummy_head.next; // 更新链表头

    // 建立页表映射
    // --- 修复: uvm_map_pages -> pmem_alloc + vm_mappages ---
    uint64 va_end = begin + npages * PGSIZE;
    for (uint64 va_current = begin; va_current < va_end; va_current += PGSIZE) {
        void *pa = pmem_alloc(false); // (false) 为用户空间分配物理页
        if (pa == 0) {
            uvm_munmap(p, begin, len); // 映射失败, 回滚 (调用修复后的 munmap)
            return -1;
        }
        
        // --- 修复: 移除 if 检查 ---
        vm_mappages(p->pgtbl, va_current, (uint64)pa, PGSIZE, prot | PTE_U);
    }
    
    // 调试性输出
    printf("--- sys_mmap: mmap list ---\n");
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl); // <--- 依赖 mem/method.h
    printf("\n");
    
    return begin;
}


int uvm_munmap(proc_t *p, uint64 begin, uint64 len)
{
    uint32 npages = PGROUNDUP(len) / PGSIZE;
    uint64 end = begin + npages * PGSIZE;
    if (npages == 0) return 0;
    
    // 创建虚拟头节点
    mmap_region_t dummy_head;
    dummy_head.begin = 0;
    dummy_head.npages = 0;
    dummy_head.next = p->mmap;

    mmap_region_t *pre = &dummy_head;
    mmap_region_t *cur = p->mmap;

    while (cur) {
        uint64 cur_end = cur->begin + cur->npages * PGSIZE;

        // 检查释放区域 [begin, end) 与当前节点 [cur->begin, cur_end) 的关系
        
        // 1. 完全不相交 (cur 在释放区域左侧)
        if (cur_end <= begin) {
            pre = cur;
            cur = cur->next;
            continue;
        }

        // 2. 完全不相交 (cur 在释放区域右侧)
        if (cur->begin >= end) {
            break; // 后面的都不可能相交
        }

        // 3. 释放区域覆盖了整个 cur 节点
        if (begin <= cur->begin && end >= cur_end) {
            vm_unmappages(p->pgtbl, cur->begin, cur->npages * PGSIZE, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
            pre->next = cur->next;
            mmap_region_free(cur);
            cur = pre->next;
            continue;
        }

        // 4. cur 节点覆盖了整个释放区域 (需要分裂)
        if (cur->begin < begin && cur_end > end) {
            // uint32 old_npages = cur->npages; // <--- 修复: 移除未使用的变量
            uint64 old_end = cur_end;

            // 分裂: cur 节点保留左侧
            cur->npages = (begin - cur->begin) / PGSIZE;
            
            // 创建新节点 (右侧)
            mmap_region_t *new_node = mmap_region_alloc();
            if (new_node == 0) return -1; // 内存不足
            new_node->begin = end;
            new_node->npages = (old_end - end) / PGSIZE;
            new_node->next = cur->next;
            cur->next = new_node;

            // 解除中间区域的映射
            vm_unmappages(p->pgtbl, begin, npages * PGSIZE, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
            
            // cur 已经处理完毕, pre 和 cur 都需要后移
            pre = new_node;
            cur = new_node->next;
            continue;
        }

        // 5. 释放区域覆盖了 cur 的左侧部分
        if (begin <= cur->begin && end < cur_end) {
            uint32 unmap_npages = (end - cur->begin) / PGSIZE;
            vm_unmappages(p->pgtbl, cur->begin, unmap_npages * PGSIZE, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
            
            // cur 节点右移, 页数减少
            cur->begin = end;
            cur->npages = (cur_end - end) / PGSIZE;

            // pre 不变, cur 不变 (继续检查 cur)
            continue;
        }

        // 6. 释放区域覆盖了 cur 的右侧部分
        if (begin > cur->begin && end >= cur_end) {
            uint32 unmap_npages = (cur_end - begin) / PGSIZE;
            vm_unmappages(p->pgtbl, begin, unmap_npages * PGSIZE, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages

            // cur 节点页数减少
            cur->npages = (begin - cur->begin) / PGSIZE;
            
            // pre 后移, cur 后移
            pre = cur;
            cur = cur->next;
            continue;
        }

        // 7. (debug) 理论上不应该到这里
        printf("uvm_munmap: unhandled case\n");
        pre = cur;
        cur = cur->next;
    }

    p->mmap = dummy_head.next; // 更新链表头

    // 调试性输出
    printf("--- sys_munmap: mmap list ---\n");
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");

    return 0;
}


// 辅助函数: 打印 mmap 链表 (用于调试)
void uvm_show_mmaplist(mmap_region_t *list)
{
    mmap_region_t *cur = list;
    printf("mmap_list: ");
    while (cur) {
        printf("[0x%x, %d pages] -> ", cur->begin, cur->npages);
        cur = cur->next;
    }
    printf("null\n");
}


/*
 * ---------------- uvm_pgtbl ----------------
 */

// 递归释放页表
static void pgtbl_free_recursive(pgtbl_t pgtbl, int level)
{
    if (level < 0) return;

    for (int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // 这是一个指向下一级页表的 PTE
            uint64 child_pa = PTE_TO_PA(pte); // <--- 修复: PTE2PA -> PTE_TO_PA
            pgtbl_free_recursive((pgtbl_t)child_pa, level - 1);
        }
    }
    pmem_free((uint64)pgtbl, true); // <--- 修复: 增加 (uint64) 和 'true'
}

void uvm_destroy_pgtbl(proc_t *p)
{
    // 1. 释放 mmap 区域的物理页和 mmap_region 节点
    mmap_region_t *cur = p->mmap;
    while (cur) {
        vm_unmappages(p->pgtbl, cur->begin, cur->npages * PGSIZE, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
        mmap_region_t *next = cur->next;
        mmap_region_free(cur);
        cur = next;
    }
    p->mmap = 0;

    // 2. 释放 heap 区域的物理页
    uint64 heap_pg = PGROUNDUP(p->heap_top);
    if (heap_pg > UHEAP_START) { // <--- 依赖 mem/type.h
        vm_unmappages(p->pgtbl, UHEAP_START, heap_pg - UHEAP_START, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
    }
    p->heap_top = UHEAP_START;

    // 3. 释放 ustack 区域的物理页
    uint64 ustack_bottom = VA_MAX - (p->ustack_npage + 2) * PGSIZE;
    if (p->ustack_npage > 1) {
        vm_unmappages(p->pgtbl, ustack_bottom, (p->ustack_npage - 1) * PGSIZE, true); // <--- 修复: uvm_unmap_pages -> vm_unmappages
    }
    // (保留 ustack_top 下方的一页)
    vm_unmappages(p->pgtbl, VA_MAX - 2 * PGSIZE, PGSIZE, true); // <--- 修复: 释放初始栈页
    p->ustack_npage = 0;

    // 4. 释放 initcode (如果存在)
    vm_unmappages(p->pgtbl, 0, PGSIZE, true); // <--- 修复: 假设 initcode 在 0

    // 5. 递归释放页表本身 (除了 trampoline)
    if (p->pgtbl) {
        // uvm_unmap(p->pgtbl, TRAMPOLINE, PGSIZE); // TRAMPOLINE 是共享的, 不unmap
        pgtbl_free_recursive(p->pgtbl, 2); // 从L2开始
        p->pgtbl = 0;
    }
}


int uvm_copy_pgtbl(proc_t *p_old, proc_t *p_new)
{
    // 1. 创建新页表, 映射 TRAMPOLINE
    p_new->pgtbl = (pgtbl_t)pmem_alloc(true); // <--- 修复: 增加 'true'
    if (p_new->pgtbl == 0) return -1;
    memset(p_new->pgtbl, 0, PGSIZE);
    kvm_map_trampoline(p_new->pgtbl); // <--- 依赖 mem/method.h

    // 2. 复制 heap
    uint64 heap_size = p_old->heap_top - UHEAP_START;
    if (heap_size > 0) {
        // --- 修复: uvm_map_pages -> pmem_alloc + vm_mappages ---
        uint64 heap_end_pg = PGROUNDUP(p_old->heap_top);
        for (uint64 va = UHEAP_START; va < heap_end_pg; va += PGSIZE) {
            void *pa = pmem_alloc(false);
            if (pa == 0) goto fail;
            
            // --- 修复: 移除 if 检查 ---
            vm_mappages(p_new->pgtbl, va, (uint64)pa, PGSIZE, PTE_U | PTE_R | PTE_W);
        }
        
        // 复制 heap 内容
        if (uvm_copyin(p_old->pgtbl, (char *)p_new->pgtbl + UHEAP_START, UHEAP_START, heap_size) < 0) {
            // 简化的方法: 直接物理内存复制
            for (uint64 va = UHEAP_START; va < PGROUNDUP(p_old->heap_top); va += PGSIZE) {
                pte_t *pte_old = vm_getpte(p_old->pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
                pte_t *pte_new = vm_getpte(p_new->pgtbl, va, true); // <--- 修复: walk_pte -> vm_getpte
                if (pte_old && (*pte_old & PTE_V) && pte_new && (*pte_new & PTE_V)) {
                    void *pa_old = (void *)PTE_TO_PA(*pte_old); // <--- 修复: PTE2PA -> PTE_TO_PA
                    void *pa_new = (void *)PTE_TO_PA(*pte_new); // <--- 修复: PTE2PA -> PTE_TO_PA
                    memmove(pa_new, pa_old, PGSIZE);
                }
            }
        }
    }
    p_new->heap_top = p_old->heap_top;

    // 3. 复制 ustack
    p_new->ustack_npage = p_old->ustack_npage;
    uint64 ustack_bottom_old = VA_MAX - (p_old->ustack_npage + 2) * PGSIZE;
    uint64 ustack_size = p_old->ustack_npage * PGSIZE;
    
    // --- 修复: uvm_map_pages -> pmem_alloc + vm_mappages ---
    if (ustack_size > 0) {
        for (uint64 va = ustack_bottom_old; va < VA_MAX - 2 * PGSIZE; va += PGSIZE) {
            void *pa = pmem_alloc(false);
            if (pa == 0) goto fail;
            
            // --- 修复: 移除 if 检查 ---
            vm_mappages(p_new->pgtbl, va, (uint64)pa, PGSIZE, PTE_U | PTE_R | PTE_W);
        }
    }
    
    // 复制 ustack 内容
    for (uint64 va = ustack_bottom_old; va < VA_MAX - 2 * PGSIZE; va += PGSIZE) {
        pte_t *pte_old = vm_getpte(p_old->pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
        pte_t *pte_new = vm_getpte(p_new->pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
        if (pte_old && (*pte_old & PTE_V) && pte_new && (*pte_new & PTE_V)) {
            void *pa_old = (void *)PTE_TO_PA(*pte_old); // <--- 修复: PTE2PA -> PTE_TO_PA
            void *pa_new = (void *)PTE_TO_PA(*pte_new); // <--- 修复: PTE2PA -> PTE_TO_PA
            memmove(pa_new, pa_old, PGSIZE);
        }
    }

    // 4. 复制 mmap 区域
    mmap_region_t *cur_old = p_old->mmap;
    mmap_region_t **p_cur_new = &(p_new->mmap); // 指向新链表尾部的 next 指针
    
    while (cur_old) {
        mmap_region_t *node_new = mmap_region_alloc();
        if (node_new == 0) goto fail;
        
        node_new->begin = cur_old->begin;
        node_new->npages = cur_old->npages;
        node_new->next = 0;

        // 映射
        // --- 修复: uvm_map_pages -> pmem_alloc + vm_mappages ---
        uint64 va_end = node_new->begin + node_new->npages * PGSIZE;
        for (uint64 va = node_new->begin; va < va_end; va += PGSIZE) {
            void *pa = pmem_alloc(false);
            if (pa == 0) {
                mmap_region_free(node_new);
                goto fail;
            }
            
            // --- 修复: 移除 if 检查 ---
            // (假设 prot, R W X U)
            vm_mappages(p_new->pgtbl, va, (uint64)pa, PGSIZE, PTE_U | PTE_R | PTE_W | PTE_X);
        }

        // 复制内容
        for (uint64 va = node_new->begin; va < node_new->begin + node_new->npages * PGSIZE; va += PGSIZE) {
            pte_t *pte_old = vm_getpte(p_old->pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
            pte_t *pte_new = vm_getpte(p_new->pgtbl, va, false); // <--- 修复: walk_pte -> vm_getpte
            if (pte_old && (*pte_old & PTE_V) && pte_new && (*pte_new & PTE_V)) {
                void *pa_old = (void *)PTE_TO_PA(*pte_old); // <--- 修复: PTE2PA -> PTE_TO_PA
                void *pa_new = (void *)PTE_TO_PA(*pte_new); // <--- 修复: PTE2PA -> PTE_TO_PA
                memmove(pa_new, pa_old, PGSIZE);
            }
        }

        // 插入新链表
        *p_cur_new = node_new;
        p_cur_new = &(node_new->next);
        cur_old = cur_old->next;
    }

    return 0;

fail:
    uvm_destroy_pgtbl(p_new); // 失败则回滚
    return -1;
}