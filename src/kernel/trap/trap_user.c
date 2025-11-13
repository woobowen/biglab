#include "mod.h"


/*
 * ---------------- trap_user ----------------
 * * TODO: 
 * 1. 完成 trap_user_handler
 * 2. 识别 scause, 分类处理
 * * NOTE:
 * 1. scause = 8: 系统调用
 * a. 调用 syscall()
 * b. sepc += 4
 * 2. scause = 13 / 15: page fault
 * a. 打印调试信息
 * b. 调用 uvm_ustack_grow()
 * c. 成功, sepc 不变 (重新执行)
 * d. 失败, kill_proc()
 * 3. 其他: 暂不处理, kill_proc()
 */
extern char kernel_vector[];
void trap_user_handler()
{
    proc_t *p = myproc();
    uint64 scause = r_scause();
    uint64 sepc = r_sepc();
    uint64 stval = r_stval();
    p->tf->user_to_kern_epc = sepc; // 保存 sepc 到 trapframe
    w_stvec((uint64)kernel_vector);
    
    if (scause == 8) {
        // System call from User-mode
        
        // sepc + 4, 否则会陷入无限循环
        p->tf->user_to_kern_epc += 4;
        
        // 调用系统调用处理函数
        // a0 寄存器(tf->a0)用于存放返回值
        syscall();
        
    } else if (scause == 13 || scause == 15) {
        // Page Fault (Load or Store)
        
        printf("--- Page Fault: proc %d ---\n", p->pid);
        printf("scause: %d, sepc: 0x%x, stval: 0x%x\n", scause, sepc, stval);

        if (uvm_ustack_grow(p, stval) < 0) {
            // 栈扩展失败, 或 stval 不是合法的栈地址
            printf("uvm_ustack_grow failed, kill proc\n");
            kill_proc(p);
        }
        
        // 成功: sepc 不变, 重新执行指令
        
    } else {
        printf("unexpected user trap\n");
        printf("scause: %d, sepc: 0x%x, stval: 0x%x\n", scause, sepc, stval);
        kill_proc(p);
    }
    
    trap_user_return();
}