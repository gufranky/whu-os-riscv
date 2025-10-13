#ifndef __PLIC_H__
#define __PLIC_H__

#include "common.h"

void plic_init(void);          // 设置中断优先级
void plic_inithart(void);      // 使能中断开关
int  plic_claim(void);         // 获取中断号
void plic_complete(int irq);   // 告知中断响应完成

#endif