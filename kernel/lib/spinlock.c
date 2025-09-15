#include "lib/lock.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "riscv.h"

// 带层数叠加的关中断
void push_off(void)
{

}

// 带层数叠加的开中断
void pop_off(void)
{

}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{

}

// 自选锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{

}

// 获取自选锁
void spinlock_acquire(spinlock_t *lk)
{    

} 

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{

}