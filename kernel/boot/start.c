#include "riscv.h"
#include "dev/timer.h"

void main();
__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU];

void start()
{
  // 设置每个核心的 tp 寄存器为其 hartid
  w_tp(r_mhartid());

  // 在M-mode下初始化时钟中断
  timer_init();

  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_medeleg(0xffff);
  // 委托除了时钟中断之外的所有中断给S-mode
  // 时钟中断(bit 7)留在M-mode处理
  w_mideleg(0xffff & ~(1 << 7));
  w_sie(r_sie() | SIE_SEIE | SIE_SSIE);
  w_mstatus(x);//将权限寄存器调为Supervisor模式
  w_mepc((uint64)main);//设置mret的返回位置
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);//将所有内存设置为可读可写可执行
  asm volatile("mret");
}