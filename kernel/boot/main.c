#include "riscv.h"
#include "lib/print.h"
#include "dev/uart.h"
#include "proc/proc.h"
volatile static int started = 0;

int main()
{
    if(mycpuid() == 0){
    uart_init();
    printf("xv6 kernel is booting\n");
    started = 1;
    }
    else while(started == 0);
    printf("Hello from CPU %d\n", mycpuid());
    while (1);    
}