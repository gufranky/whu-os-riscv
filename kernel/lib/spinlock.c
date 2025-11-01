#include "lib/lock.h"
#include "lib/print.h"
#include "proc/cpu.h"
#include "riscv.h"

// 带层数叠加的关中断
void
push_off(void)
{
  int old = intr_get();

  // disable interrupts to prevent an involuntary context
  // switch while using mycpu().
  intr_off();

  if(mycpu()->noff == 0)
    mycpu()->origin = old;
  mycpu()->noff += 1;
}

// 带层数叠加的开中断
void pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->origin)
    intr_on();
}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{
    return false;
}

// 自选锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
    lk->locked = 0;
    lk->name = name;
    lk->cpuid = 0;
}

// 获取自选锁
void spinlock_acquire(spinlock_t *lk)
{    
  push_off();
  if(holding(lk))
    panic("acquire");
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
  lk->cpuid = mycpuid();
} 

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
if(!holding(lk))
    panic("release");
  lk->cpuid = -1;
  __sync_synchronize();
  __sync_lock_release(&lk->locked);

  pop_off();
}


int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpuid == mycpuid());
  return r;
}