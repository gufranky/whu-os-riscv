#include "proc/proc.h"
#include "riscv.h"

static cpu_t cpus[NCPU];
int
cpuid()
{
  int id = r_tp();
  return id;
}
cpu_t* mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

int mycpuid(void) 
{
    int id = cpuid();
    return id;
}
