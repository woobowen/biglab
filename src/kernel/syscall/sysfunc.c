#include "lib/type.h"
#include "lib/method.h"
#include "syscall/type.h"
#include "syscall/method.h"
#include "proc/type.h"
#include "proc/method.h"
#include "mem/type.h"
#include "mem/method.h"

/*
 * ---------------- sys_helloworld ----------------
 */

uint64 sys_helloworld()
{
    printf("hello, world!\n");
    return 0;
}

/*
 * ---------------- sys_copyin / sys_copyout / sys_copyinstr ----------------
 * * TODO: 
 * 1. 完成 sys_copyin / sys_copyout / sys_copyinstr
 * 2. 从 trapframe 中获取参数
 * 3. 调用 uvm_copyin / uvm_copyout / uvm_copyin_str
 */

// 内核中的测试数组
int test_L[5] = {1, 2, 3, 4, 5};

uint64 sys_copyout()
{
    proc_t *p = myproc();
    uint64 user_L_va = p->tf->a0; // 第一个参数: L
    
    printf("--- sys_copyout: kernel ---\n");
    printf("user_L_va: 0x%x\n", user_L_va);

    return uvm_copyout(p->pgtbl, user_L_va, (char *)test_L, sizeof(test_L));
}


uint64 sys_copyin()
{
    proc_t *p = myproc();
    uint64 user_L_va = p->tf->a0; // 第一个参数: L
    uint64 len = p->tf->a1;       // 第二个参数: 5

    int kernel_L[5];
    
    if (uvm_copyin(p->pgtbl, (char *)kernel_L, user_L_va, len * sizeof(int)) < 0)
        return -1;

    printf("--- sys_copyin: kernel ---\n");
    for (int i = 0; i < len; i++) {
        printf("kernel_L[%d] = %d\n", i, kernel_L[i]);
    }
    
    return 0;
}


uint64 sys_copyinstr()
{
    proc_t *p = myproc();
    uint64 user_s_va = p->tf->a0; // 第一个参数: s
    
    char kernel_s[128]; // 假设最大 128
    
    if (uvm_copyin_str(p->pgtbl, kernel_s, user_s_va, 128) < 0)
        return -1;
    
    printf("--- sys_copyinstr: kernel ---\n");
    printf("kernel_s: %s\n", kernel_s);
    
    return 0;
}


/*
 * ---------------- sys_brk ----------------
 * * TODO: 
 * 1. 完成 sys_brk
 * 2. 从 trapframe 中获取参数 new_top
 * 3. new_top == 0, 返回 p->heap_top
 * 4. new_top > old_top, uvm_heap_grow()
 * 5. new_top < old_top, uvm_heap_ungrow()
 */

uint64 sys_brk()
{
    proc_t *p = myproc();
    uint64 new_top = p->tf->a0;
    uint64 old_top = p->heap_top;

    if (new_top == 0) {
        // printf("sys_brk: query heap_top = 0x%x\n", old_top);
        return old_top;
    }

    if (new_top > old_top) {
        printf("--- sys_brk: grow ---\n");
        printf("old_top: 0x%x, new_top: 0x%x\n", old_top, new_top);
        if (uvm_heap_grow(p, new_top) < 0)
            return -1;
    } else if (new_top < old_top) {
        printf("--- sys_brk: ungrow ---\n");
        printf("old_top: 0x%x, new_top: 0x%x\n", old_top, new_top);
        uvm_heap_ungrow(p, new_top);
    }
    
    return p->heap_top;
}

/*
 * ---------------- sys_mmap / sys_munmap ----------------
 * * TODO: 
 * 1. 完成 sys_mmap / sys_munmap
 * 2. 从 trapframe 中获取参数
 * 3. 调用 uvm_mmap / uvm_munmap
 */

uint64 sys_mmap()
{
    proc_t *p = myproc();
    uint64 begin = p->tf->a0;
    uint64 len = p->tf->a1;
    
    // 权限暂定为 R | W
    return uvm_mmap(p, begin, len, PTE_R | PTE_W);
}

uint64 sys_munmap()
{
    proc_t *p = myproc();
    uint64 begin = p->tf->a0;
    uint64 len = p->tf->a1;
    
    return uvm_munmap(p, begin, len);
}