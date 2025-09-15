#include "riscv.h"
void main();
__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void start()
{
  // 设置每个核心的 tp 寄存器为其 hartid
  w_tp(r_mhartid());
  
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);//将权限寄存器调为Supervisor模式
  w_mepc((uint64)main);//设置mret的返回位置
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);//将所有内存设置为可读可写可执行
  asm volatile("mret");
}