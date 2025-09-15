#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0){
    uart_init();
    printf("xv6 kernel is booting\n");
    started = 1;
    }
    else while(started == 0);
    printf("Hello from CPU %d\n", cpuid);
    while (1);    
}