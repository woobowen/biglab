#include "mod.h"

// 修复: 添加 trampoline.S 中定义的汇编符号的声明
extern char trampoline[];  // TRAMPOLINE 物理页的起始地址
extern char user_vector[]; // user_vector (用户态陷阱入口) 的地址
extern char user_return[]; // user_return (返回用户态) 的函数地址

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
    proc_t *p = myproc(); //
    uint64 scause = r_scause(); //
    uint64 sepc = r_sepc(); //
    uint64 stval = r_stval(); //
    p->tf->user_to_kern_epc = sepc; // 保存 sepc 到 trapframe
    w_stvec((uint64)kernel_vector); //
    
    if (scause == 8) {
        // System call from User-mode
        
        // sepc + 4, 否则会陷入无限循环
        p->tf->user_to_kern_epc += 4; //
        
        // 调用系统调用处理函数
        // a0 寄存器(tf->a0)用于存放返回值
        syscall(); //
        
    } else if (scause == 13 || scause == 15) {
        // Page Fault (Load or Store)
        
        printf("--- Page Fault: proc %d ---\n", p->pid); //
        printf("scause: %d, sepc: 0x%x, stval: 0x%x\n", scause, sepc, stval); //

        if (uvm_ustack_grow(p, stval) < 0) { //
            // 栈扩展失败, 或 stval 不是合法的栈地址
            printf("uvm_ustack_grow failed, kill proc\n"); //
            kill_proc(p); //
        }
        
        // 成功: sepc 不变, 重新执行指令
        
    } else {
        printf("unexpected user trap\n"); //
        printf("scause: %d, sepc: 0x%x, stval: 0x%x\n", scause, sepc, stval); //
        kill_proc(p); //
    }
    
    trap_user_return(); //
}

// 修复: 添加 trap_user_return C函数的实现
void trap_user_return()
{
    proc_t *p = myproc(); //

    // 1. 切换回 S 态的陷阱处理函数为 user_vector
    //    (user_vector 位于 TRAMPOLINE, 需要计算其虚拟地址)
    //   
    uint64 user_stvec = TRAMPOLINE + (user_vector - trampoline);
    w_stvec(user_stvec); //

    // 2. 准备调用汇编函数 user_return
    //    它需要两个参数 (见 trampoline.S): 
    //    a0 = trapframe 的地址 (p->tf)
    //    a1 = 用户的页表 (satp 格式)
    
    //
    uint64 satp = MAKE_SATP(p->pgtbl); 
    
    // 3. 计算汇编函数 user_return 的地址
    //
    uint64 fn_user_return = TRAMPOLINE + (user_return - trampoline);

    // 4. 将 fn_user_return 转换为函数指针并调用
    //    这将跳转到 trampoline.S 中的 user_return,
    //    它负责恢复用户寄存器并执行 sret
    //
    ((void (*)(uint64, uint64))fn_user_return)( (uint64)(p->tf), satp );
}