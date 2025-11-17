#include "sys.h"

// 与内核保持一致
#define VA_MAX       (1ul << 38)
#define PGSIZE       4096
#define MMAP_END     (VA_MAX - 34 * PGSIZE)
#define MMAP_BEGIN   (MMAP_END - 8096 * PGSIZE) 

char *str1, *str2;

int main()
{
    syscall(SYS_print, "\nuser begin\n");

    // 测试MMAP区域
    //str1 = (char*)syscall(SYS_mmap, MMAP_BEGIN, PGSIZE);
    
    // 测试HEAP区域
    long long top = syscall(SYS_brk, 0);
    str2 = (char*)top;
    syscall(SYS_brk, top + PGSIZE);

    //str1[0] = 'M';
    //str1[1] = 'M';
    //str1[2] = 'A';
    //str1[3] = 'P';
    //str1[4] = '\n';
    //str1[5] = '\0';

    str2[0] = 'H';
    str2[1] = 'E';
    str2[2] = 'A';
    str2[3] = 'P';
    str2[4] = '\n';
    str2[5] = '\0';

    int pid = syscall(SYS_fork);

    if(pid == 0) { // 子进程
        for(int i = 0; i < 100000000; i++);
        syscall(SYS_print, "child: hello\n");
        //syscall(SYS_print, str1);
        syscall(SYS_print, str2);

        syscall(SYS_exit, 1);
        syscall(SYS_print, "child: never back\n");
    } else {       // 父进程
        int exit_state;
        syscall(SYS_wait, &exit_state);
        if(exit_state == 1)
            syscall(SYS_print, "parent: hello\n");
        else
            syscall(SYS_print, "parent: error\n");
    }

    while(1);
    return 0;
}