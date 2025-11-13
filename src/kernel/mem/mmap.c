#include "lib/type.h"
#include "lib/mod.h"
#include "lib/method.h"

#include "mem/type.h"
#include "lock/type.h"
#include "lock/method.h"



/*
 * ---------------- mmap_region_node ----------------
 * * TODO: 
 * 1. 完成 mmap_init / mmap_region_alloc / mmap_region_free
 * 2. mmap_init: 初始化 list_lk, list_head, 并将 node_list 
 * 中的所有节点加入 list_head (空闲链表)
 * 3. mmap_region_alloc: 从空闲链表获取一个节点
 * 4. mmap_region_free: 将一个节点归还到空闲链表
 * * NOTE:
 * 1. N_MMAP 在 mem/type.h 中定义
 * 2. list_head 是空闲链表的"头节点", 不可被分配
 * 3. "头插法"或"尾插法"均可
 * 4. 注意自旋锁的使用
 */

// mmap_region_node_t 仓库(单向链表) + 链表头节点(不可分配) + 保护仓库的自旋锁
static mmap_region_node_t node_list[N_MMAP];
static mmap_region_node_t list_head;
static spinlock_t list_lk;

void mmap_init()
{
    spinlock_init(&list_lk, "mmap_list_lock");
    list_head.next = 0;

    // 将所有 node_list 节点加入空闲链表 (头插法)
    for (int i = 0; i < N_MMAP; i++) {
        node_list[i].next = list_head.next;
        list_head.next = &node_list[i];
    }
}

mmap_region_t *mmap_region_alloc()
{
    spinlock_acquire(&list_lk);
    mmap_region_node_t *node = list_head.next;
    if (node) {
        list_head.next = node->next;
        
        // (调试性) 清空 mmap_region_t 内容
        memset(&node->mmap, 0, sizeof(mmap_region_t));
    }
    spinlock_release(&list_lk);
    
    return (node ? &node->mmap : 0);
}

void mmap_region_free(mmap_region_t *mmap)
{
    // 通过 mmap 找到 node
    // (这是一个 C 语言中基于结构体偏移的小技巧,
    //  等价于 (mmap_region_node_t *)((char *)mmap - offsetof(mmap_region_node_t, mmap)) )
    mmap_region_node_t *node = (mmap_region_node_t *)mmap;

    spinlock_acquire(&list_lk);
    node->next = list_head.next;
    list_head.next = node;
    spinlock_release(&list_lk);
}
// 辅助函数: 打印 mmap_region_node 仓库 (用于测试)
void mmap_show_nodelist()
{
    spinlock_acquire(&list_lk);
    mmap_region_node_t *cur = list_head.next;
    printf("mmap_node_list: \n");
    
    int i = 0;
    while(cur) {
        printf("node %d index = %d\n", i, cur - node_list);
        cur = cur->next;
        i++;
    }

    spinlock_release(&list_lk);
}