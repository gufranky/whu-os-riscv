#ifndef __TRAP_H__
#define __TRAP_H__

/* 
    在risc-v体系结构里trap分为interrupt和exception
    中断是同步的, 异常是异步的
    中断返回时执行下一条指令, 异常返回时重新执行发生异常的指令
    常见中断: 时钟中断 外设中断 软件中断
    常见异常: 非法指令 页面访问异常 断点异常
*/

#include "common.h"

// trap的初始化和处理

void trap_kernel_init();
void trap_kernel_inithart();
void trap_kernel_handler();

// 辅助函数: 外设中断和时钟中断处理

void external_interrupt_handler();
void timer_interrupt_handler();

#endif