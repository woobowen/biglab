#include "mod.h"
#include "../proc/mod.h"

// 睡眠锁初始化
void sleeplock_init(sleeplock_t *lk, char *name)
{
    spinlock_init(&lk->lock, "sleeplock"); // 内部自旋锁
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0; // 无进程持有锁（pid 从1开始）
}

// 检查当前进程是否持有睡眠锁
bool sleeplock_holding(sleeplock_t *lk)
{
    bool ret;
    spinlock_acquire(&lk->lock); 
    // 检查是否上锁且持有者是当前进程
    ret = lk->locked && (lk->pid == myproc()->pid);
    spinlock_release(&lk->lock);
    return ret;
}

// 当前进程尝试获取睡眠锁, 失败进入睡眠状态
void sleeplock_acquire(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    // 如果锁已经被占用，则循环睡眠等待
    while (lk->locked) {
        // proc_sleep 会释放 lk->lock 并进入睡眠
        // 直到被唤醒后重新获取 lk->lock
        // 因此不需要手动 release 再 acquire
        proc_sleep((void *)lk, &lk->lock);
    }

    // 成功抢到锁
    lk->locked = 1;
    lk->pid = myproc()->pid; 
    spinlock_release(&lk->lock);
}

// 释放睡眠锁, 唤醒其他等待睡眠锁的进程
void sleeplock_release(sleeplock_t *lk)
{
    spinlock_acquire(&lk->lock);
    // 只有持有锁的进程才能释放锁
    if (!lk->locked || lk->pid != myproc()->pid) {
        panic("sleeplock_release: not holding the lock");
    }

    lk->locked = 0;
    lk->pid = 0;

    // 唤醒所有在该锁上睡眠的进程
    proc_wakeup((void *)lk);
    spinlock_release(&lk->lock);
}