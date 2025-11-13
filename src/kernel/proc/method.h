#pragma once

// 1. 在这里添加头文件包含
#include "type.h"

// proc.c: 页表初始化 + 第一个进程初始化

pgtbl_t proc_pgtbl_init(uint64 trapframe);
void proc_make_first();

// 2. 在这里添加 kill_proc 的声明
void kill_proc(proc_t *p);