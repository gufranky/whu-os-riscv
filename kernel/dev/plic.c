// Platform-level interrupt controller

#include "memlayout.h"
#include "dev/plic.h"
#include "proc/cpu.h"

// PLIC初始化
void plic_init()
{
    // 设置中断优先级
    *(uint32*)(PLIC_PRIORITY(UART_IRQ)) = 1;
}

// PLIC核心初始化
void plic_inithart()
{   
    int hartid = mycpuid();
    // 使能中断开关
    *(uint32*)PLIC_SENABLE(hartid) = (1 << UART_IRQ);
    // 设置响应阈值
    *(uint32*)PLIC_SPRIORITY(hartid) = 0;
}

// 获取中断号
int plic_claim(void)
{
    int hartid = mycpuid();
    int irq = *(uint32*)PLIC_SCLAIM(hartid);
    return irq;
}

// 确认该中断号对应中断已经完成
void plic_complete(int irq)
{
    int hartid = mycpuid();
    *(uint32*)PLIC_SCLAIM(hartid) = irq;
}