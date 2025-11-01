#include "proc/cpu.h"
#include "riscv.h"

static cpu_t cpus[NCPU];

cpu_t* mycpu(void)
{
    int id = mycpuid();
    return &cpus[id];
}

int mycpuid(void)
{
    return r_tp();
}

proc_t* myproc(void)
{
    return mycpu()->proc;
}