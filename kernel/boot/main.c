#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "trap/trap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/proc.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        // CPU 0 初始化
        uart_init();
        print_init();

        // 初始化时钟系统
        timer_create();

        // 初始化物理内存管理
        pmem_init();

        // 初始化内核页表和虚拟内存
        kvm_init();
        kvm_inithart();

        // 初始化trap处理
        trap_kernel_init();
        trap_kernel_inithart();

        // 初始化进程管理
        proc_init();

        printf("cpu %d is booting! Kernel initialized.\n", cpuid);
        __sync_synchronize();
        started = 1;

    } else {
        // CPU 1 等待CPU 0完成初始化
        while(started == 0);
        __sync_synchronize();

        // 初始化页表
        kvm_inithart();

        // 初始化trap处理
        trap_kernel_inithart();

        printf("cpu %d is booting! Kernel initialized.\n", cpuid);
    }

    // 启用中断
    intr_on();

    if(cpuid == 0) {
        // CPU 0 创建并切换到第一个用户进程
        printf("CPU %d: Creating first user process...\n", cpuid);
        proc_make_first();
        // proc_make_first() 不会返回，直接切换到用户进程
        //panic("proc_make_first returned");
    } else {
        // CPU 1 进入空闲循环等待时钟中断
        printf("CPU %d: Entering idle loop, waiting for interrupts...\n", cpuid);
        while (1) {
            // CPU 1 空闲等待
        }
    }
    proc_scheduler();
    return 0;
}
