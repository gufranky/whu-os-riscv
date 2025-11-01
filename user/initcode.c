#include "sys.h"

int main()
{
    syscall(SYS_print);
    syscall(SYS_print);
    while(1);
    return 0;
}