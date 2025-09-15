#ifndef __CPU_H__
#define __CPU_H__

#include "common.h"

typedef struct cpu {
    int noff;       // 关中断的深度
    int origin;     // 第一次关中断前的状态
} cpu_t;

int     mycpuid(void);
cpu_t*  mycpu(void);

#endif