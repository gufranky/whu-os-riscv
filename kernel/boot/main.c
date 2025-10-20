#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "trap/trap.h"

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

        // 初始化trap处理
        trap_kernel_init();
        trap_kernel_inithart();

        printf("cpu %d is booting! Timer interrupt test starting...\n", cpuid);
        __sync_synchronize();
        started = 1;

    } else {
        // CPU 1 等待CPU 0完成初始化
        while(started == 0);
        __sync_synchronize();

        // 初始化trap处理
        trap_kernel_inithart();

        printf("cpu %d is booting! Timer interrupt enabled.\n", cpuid);
    }

    // 启用中断
    intr_on();

    printf("CPU %d: Timer interrupt test running. Look for 't' characters...\n", cpuid);

    // 无限循环等待时钟中断
    while (1) {
    }
}
