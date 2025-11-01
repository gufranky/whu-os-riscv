#ifndef __CPU_H__
#define __CPU_H__

#include "common.h"
#include "proc/proc.h"

typedef struct cpu {
    int noff;       // 关中断的深度
    int origin;     // 第一次关中断前的状态
    proc_t* proc;   // cpu上运行的进程
    context_t ctx;  // 内核上下文暂存
} cpu_t;

int     mycpuid(void);
cpu_t*  mycpu(void);
proc_t* myproc(void);

#endif