#include "mod.h"

// 这个文件通过make build生成, 是proczero对应的ELF文件
#include "../../user/initcode.h"
#define initcode target_user_initcode
#define initcode_len target_user_initcode_len

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t *old, context_t *new);

// in trap/trap_user.c
extern void trap_user_return();

/* ------------本地变量----------- */

// 进程结构体数组 + 第一个用户进程的指针
static proc_t proc_list[N_PROC];
static proc_t *proczero;

// 全局pid + 保护它的锁
static int global_pid;
static spinlock_t pid_lk;

/* 获取一个pid */
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&pid_lk);
    assert(global_pid > 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&pid_lk);
    return tmp;
}

/* 释放进程锁 + trap_user_return */
static void proc_return()
{
    proc_t *p = myproc();

    spinlock_release(&p->lk); // 先释放锁
    // 如果是第一个进程(proczero)，则进行特殊初始化
    if (p->pid == 1) {
        // 初始化文件系统
        fs_init();
        // 设置open_file: 打开stdin, stdout, stderr
        p->open_file[0] = file_open("/dev/stdin", FILE_OPEN_READ);
        p->open_file[1] = file_open("/dev/stdout", FILE_OPEN_WRITE);
        p->open_file[2] = file_open("/dev/stderr", FILE_OPEN_WRITE);
        // 设置cwd为根目录
        p->cwd = inode_get(ROOT_INODE);
    }

    // 回到用户态
    trap_user_return();
}

/* 进程模块初始化 */
void proc_init()
{    
    // 初始化全局 pid 与其锁
    global_pid = 1;
    spinlock_init(&pid_lk, "pid_lock");

    // 初始化进程数组
    for (int i = 0; i < N_PROC; i++) {
        memset(&proc_list[i], 0, sizeof(proc_t)); // 初始化为null
        spinlock_init(&proc_list[i].lk, "proc_lock");
    }
    // proczero 指向第一个槽位
    proczero = &proc_list[0];
}

/* 
    申请一个UNUSED进程结构体(返回时带锁)
    并执行通用的初始化逻辑
*/
proc_t *proc_alloc()
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        // 试图占用该进程结构体
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED) {
            memset(p->name, 0, sizeof(p->name));
            p->pid = alloc_pid();
            p->exit_code = 0;
            p->sleep_space = NULL;
            p->parent = myproc();
            p->pgtbl = NULL;
            p->heap_top = 0;
            p->ustack_npage = 0;
            p->mmap = NULL;
            p->tf = NULL;
            // 预设内核栈与上下文
            p->kstack = (uint64)KSTACK(i);
            p->ctx.ra = (uint64)proc_return; // 切入到该进程时，从这里返回到用户态入口
            p->ctx.sp = p->kstack + PGSIZE;
            return p; // 保持锁定返回
        }else{
            spinlock_release(&p->lk);
        }
    }
    return NULL;
}

/* 
    回收一个进程结构体并释放它包含的资源
    tips: 调用者需要持有进程锁
*/
void proc_free(proc_t *p)
{
    // 释放用户态页表相关资源
    if (p->pgtbl) {
        uvm_destroy_pgtbl(p->pgtbl); 
        p->pgtbl = NULL;
    }

    // 释放open_file
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (p->open_file[i]) {
            file_close(p->open_file[i]);
            p->open_file[i] = NULL;
        }
    }
    // 释放cwd
    if (p->cwd) {
        inode_put(p->cwd);
        p->cwd = NULL;
    }

    // 清空结构体并置为 UNUSED
    memset(p->name, 0, sizeof(p->name));
    p->pid = 0;
    p->parent = NULL;
    p->tf = NULL;
    p->exit_code = 0;
    p->sleep_space = NULL;
    p->pgtbl = NULL;
    p->heap_top = 0;
    p->ustack_npage = 0;
    p->mmap = NULL;
    p->kstack = 0;
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->state = UNUSED;
}

/* 
    获得一个初始化过的用户页表
    完成trapframe和trampoline的映射
*/
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 1. 分配一页作为用户根页表
    pgtbl_t upgtbl = (pgtbl_t)pmem_alloc(true);
    if (!upgtbl) {
        panic("proc_pgtbl_init: pmem_alloc failed");
    }
    memset(upgtbl, 0, PGSIZE);  // 清零

    // 2. 在用户页表中映射 trampoline 与 trapframe
    vm_mappages(upgtbl, (uint64)TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);  // 可读、不可写、可执行
    vm_mappages(upgtbl, (uint64)TRAPFRAME, (uint64)trapframe, PGSIZE, PTE_R | PTE_W);  //需要读写

    return upgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问

	注意: 用用户空间的地址映射需要标记 PTE_U
*/
void proc_make_first()
{
    printf("I'm here.");
    // 通过通用分配接口申请 proczero（返回时持有锁）
    proc_t *p = proc_alloc();
    if (!p) panic("proc_make_first: proc_alloc failed");
    p->pid = 1; // proczero 固定为 pid=1

    // 1. 申请trapframe的物理页
    trapframe_t *tf = (trapframe_t *)pmem_alloc(false);
    if (!tf) {
        panic("proc_make_first: pmem_alloc for trapframe failed");
    }
    memset(tf, 0, PGSIZE);  // 清零

    // 2. 调用proc_pgtbl_init：申请用户页表，并映射trampoline和trapframe
    pgtbl_t upgtbl = proc_pgtbl_init((uint64)tf);
    if (!upgtbl) {
        panic("proc_make_first: proc_pgtbl_init failed");
    }

    // 3. 准备用户地址空间其他部分
    // 3.1 空洞（1页） [0, PGSIZE) 不映射
    
    // 3.2 为ELF文件(code + data)申请一个物理页[PGSIZE, 2*PGSIZE)、进行数据转移、完成映射
    const uint64 UCODE_VA = PGSIZE; // 起始虚拟地址
    void *ucode_pa = pmem_alloc(false);
    if (!ucode_pa) {
        panic("proc_make_first: pmem_alloc for ucode failed");
    }
    memset(ucode_pa, 0, PGSIZE);  // 清零
    if (initcode_len > PGSIZE) { // 长度检查
        panic("proc_make_first: initcode too big");
    }
    memmove(ucode_pa, initcode, (uint32)initcode_len);
    vm_mappages(upgtbl, UCODE_VA, (uint64)ucode_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    // 3.3 用户栈ustack（1页，在trapframe之下） [TRAPFRAME - PGSIZE, TRAPFRAME)
    const uint64 USTACK_TOP = (uint64)TRAPFRAME; // 栈顶
    const uint64 USTACK_VA = USTACK_TOP - PGSIZE; // 栈底
    void *ustack_pa = pmem_alloc(false);
    if (!ustack_pa) {
        panic("proc_make_first: pmem_alloc for ustack failed");
    }
    memset(ustack_pa, 0, PGSIZE);
    vm_mappages(upgtbl,USTACK_VA,(uint64)ustack_pa,PGSIZE,PTE_R | PTE_W | PTE_U);

    // 4. 填充 proczero 结构体
    // strncpy(p->name, "proczero", sizeof(p->name)-1);
    p->pgtbl = upgtbl;
    p->heap_top = 2 * PGSIZE;
    p->ustack_npage = 1;
    p->tf = tf;
    p->mmap = NULL;
    p->state = RUNNABLE;

    // 5. 设置 trapframe 中的入口与用户栈
    tf->user_to_kern_epc = UCODE_VA;
    tf->sp = USTACK_TOP;

    // 6. 记录为 proczero 并解锁
    proczero = p;
    spinlock_release(&p->lk);
    printf("leave proc_make_first");
}

/*
    父进程产生子进程
    UNUSED -> RUNNABLE
*/
int proc_fork()
{
    proc_t *parent = myproc();
    proc_t *child = proc_alloc();
    if (!child) return -1;

    // 设置进程名字
    // strncpy(child->name, parent->name, sizeof(child->name)-1);

    // 复制trapframe
    trapframe_t *tf = (trapframe_t *)pmem_alloc(false);
    if (!tf) { spinlock_release(&child->lk); return -1; }
    *tf = *parent->tf;
    tf->user_to_kern_epc += 4;
    tf->a0=0;

    // 填充子进程结构体
    child->tf = tf;
    child->parent = parent;
    child->exit_code = 0;
    child->pgtbl = proc_pgtbl_init((uint64)tf);
    child->heap_top = parent->heap_top;
    child->ustack_npage = parent->ustack_npage;
    child->mmap = NULL; // 子进程初始无mmap
    child->state = RUNNABLE;

    // 复制页表
    uvm_copy_pgtbl(parent->pgtbl, child->pgtbl, parent->heap_top, parent->ustack_npage, parent->mmap);

    // 继承open_file
    for (int i = 0; i < N_OPEN_FILE_PER_PROC; i++) {
        if (parent->open_file[i]) {
            child->open_file[i] = file_dup(parent->open_file[i]);
        } else {
            child->open_file[i] = NULL;
        }
    }
    // 继承cwd
    if (parent->cwd) {
        child->cwd = inode_dup(parent->cwd);
    } else {
        child->cwd = NULL;
    }

    int pid = child->pid;
    spinlock_release(&child->lk);
    return pid;
}

/*
    进程主动放弃CPU控制权
    RUNNING->RUNNABLE
*/
void proc_yield()
{
    proc_t *p = myproc();
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched(); // 保持持锁切换
    spinlock_release(&p->lk);
}

/*
    当父进程退出时, 让它的所有子进程认proczero为父
    因为proczero永不退出, 可以回收子进程的资源
*/
static void proc_reparent(proc_t *parent)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        if (p == parent) continue;
        spinlock_acquire(&p->lk);
        if (p->state != UNUSED && p->parent == parent) {
            p->parent = proczero;
        }
        spinlock_release(&p->lk);
    }
}

/*
    唤醒等待呼叫的进程
    由proc_exit调用
    tips: 调用者需要持有p的进程锁
*/
static void proc_try_wakeup(proc_t *p)
{
    proc_t *parent = p->parent;
    if (!parent) return;
    // 唤醒等待“自己”的父进程
    spinlock_acquire(&parent->lk);
    if (parent->state == SLEEPING && parent->sleep_space == parent) {
        parent->state = RUNNABLE;
        parent->sleep_space = NULL;
    }
    spinlock_release(&parent->lk);
}

/*
    进程退出
    RUNNING -> ZOMBIE
*/
void proc_exit(int exit_code)
{
    proc_t *p = myproc();
    spinlock_acquire(&p->lk);
    p->exit_code = exit_code;
    // 将孩子过继给 proczero
    proc_reparent(p); 
    // 标记为 ZOMBIE 并尝试唤醒父进程
    p->state = ZOMBIE;
    proc_try_wakeup(p);

    proc_sched();
}

/*
    父进程等待一个子进程进入ZOMBIE状态
    1. 如果等到: 释放子进程, 返回子进程的pid, 将子进程的退出状态传出到user_addr
    2. 如果发现没孩子: 返回-1
    3. 如果没等到: 父进程进入睡眠状态 
*/
int proc_wait(uint64 user_addr)
{
    proc_t *parent = myproc();

    for (;;) {
        int has_child = 0;
        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];
            if (p == parent) continue; // 跳过自己
            spinlock_acquire(&p->lk);
            if (p->parent == parent && p->state != UNUSED) {
                has_child = 1;
                if (p->state == ZOMBIE) {
                    // 拷贝退出状态到用户地址
                    if (user_addr) {
                        uvm_copyout(parent->pgtbl, user_addr, (uint64)&p->exit_code, sizeof(int));
                    }
                    int pid = p->pid;
                    // 回收子进程
                    proc_free(p);
                    spinlock_release(&p->lk);
                    return pid;
                }
            }
            spinlock_release(&p->lk);
        }

        if (!has_child) {
            return -1; // 无子进程

        }
        spinlock_acquire(&parent->lk);
        // 进入睡眠，等待子进程退出
        proc_sleep(parent, &parent->lk);
        spinlock_release(&parent->lk); 
    }
}

/*
    进程等待sleep_space对应的资源, 进入睡眠状态
    RUNNING -> SLEEPING
*/
void proc_sleep(void *sleep_space, spinlock_t *lock)
{
    proc_t *p = myproc();

    // 应对外设中断处理程序调用proc_sleep的情况
    if (lock != &p->lk) {
        spinlock_acquire(&p->lk);
        spinlock_release(lock);
    }

    // 开始睡眠
    p->sleep_space = sleep_space;
    p->state = SLEEPING;
    // printf("proc %d is sleeping!\n", p->pid);

    // 切到调度器
    proc_sched();

    // 被唤醒
    // printf("proc %d is wakeup!\n", p->pid);

    // 恢复原样
    if (lock != &p->lk) {
        spinlock_release(&p->lk);
        spinlock_acquire(lock);
    }
}

/*
    唤醒所有等待sleep_space的进程
    SLEEPING -> RUNNABLE
*/
void proc_wakeup(void *sleep_space)
{
    for (int i = 0; i < N_PROC; i++) {
        proc_t *p = &proc_list[i];
        spinlock_acquire(&p->lk);
        if (p->state == SLEEPING && p->sleep_space == sleep_space) {
            p->state = RUNNABLE;
            p->sleep_space = NULL;
        }
        spinlock_release(&p->lk);
    }
}

/* 
    用户进程切换到调度器
    tips: 调用者保证持有当前进程的锁
*/
void proc_sched()
{
    cpu_t *c = mycpu();
    proc_t *p = myproc();

    // 切回原生进程
    c->proc = NULL; 
    swtch(&p->ctx, &c->ctx);
}

/* 
    调度器
    RUNNABLE->RUNNING
*/
void proc_scheduler()
{
    cpu_t *c = mycpu();
    for (;;) {
        intr_on(); // 开启中断，否则所有进程 sleep 时 CPU 会死锁在关中断状态！
        c->proc = NULL;
        for (int i = 0; i < N_PROC; i++) {
            proc_t *p = &proc_list[i];
            spinlock_acquire(&p->lk);
            if (p->state == RUNNABLE) {
                // 切入该进程
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->ctx, &p->ctx);
            }
            spinlock_release(&p->lk);
        }
    }
}