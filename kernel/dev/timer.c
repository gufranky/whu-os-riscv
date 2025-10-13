#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    int hartid = r_mhartid();

    // 设置mscratch数组，为当前CPU的时钟中断准备临时空间
    // mscratch[hartid][0-2]: 暂存a1, a2, a3寄存器
    // mscratch[hartid][3]: CLINT_MTIMECMP(hartid)地址
    // mscratch[hartid][4]: INTERVAL值
    mscratch[hartid][3] = CLINT_MTIMECMP(hartid);
    mscratch[hartid][4] = INTERVAL;

    // 设置mscratch寄存器指向当前CPU的临时空间
    w_mscratch((uint64)&mscratch[hartid]);

    // 设置M-mode中断向量
    w_mtvec((uint64)timer_vector);

    // 设置第一次时钟中断时间
    *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + INTERVAL;

    // 启用M-mode时钟中断
    w_mie(r_mie() | MIE_MTIE);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    // 初始化系统时钟
    sys_timer.ticks = 0;
    spinlock_init(&sys_timer.lk, "sys_timer");
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
    // 加锁保护共享资源
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    uint64 ticks;
    // 加锁保护共享资源
    spinlock_acquire(&sys_timer.lk);
    ticks = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    return ticks;
}