#include "sys.h"
typedef unsigned long uint64;
int main()
{
    char buf[2048];
    uint64 buf_in_kernel[10];

    // 初始状态:读了sb并释放了buf
    syscall(SYS_print, "\nstate-1:");
    syscall(SYS_show_buf);
    
    // 耗尽所有 buf
    for(int i = 0; i < 6; i++) {
        buf_in_kernel[i] = syscall(SYS_read_block, 100 + i, buf);
        buf[i] = 0xFF;
        syscall(SYS_write_block, buf_in_kernel[i], buf);
    }
    syscall(SYS_print, "\nstate-2:");
    syscall(SYS_show_buf);

    // 测试是否会触发buf_read里的panic,测试完后注释掉(一次性)
    // buf_in_kernel[0] = syscall(SYS_read_block, 0, buf);


    // 释放两个buf-4 和 buf-1，查看链的状态
    syscall(SYS_release_block, buf_in_kernel[3]);
    syscall(SYS_release_block, buf_in_kernel[0]);
    syscall(SYS_print, "\nstate-3:");
    syscall(SYS_show_buf);

    // 申请buf,测试LRU是否生效 + 测试103号block的lazy write
    buf_in_kernel[6] = syscall(SYS_read_block, 106, buf);
    buf_in_kernel[7] = syscall(SYS_read_block, 103, buf);
    syscall(SYS_print, "\nstate-4:");
    syscall(SYS_show_buf);

    // 释放所有buf
    syscall(SYS_release_block, buf_in_kernel[7]);
    syscall(SYS_release_block, buf_in_kernel[6]);
    syscall(SYS_release_block, buf_in_kernel[5]);
    syscall(SYS_release_block, buf_in_kernel[4]);
    syscall(SYS_release_block, buf_in_kernel[2]);
    syscall(SYS_release_block, buf_in_kernel[1]);
    syscall(SYS_print, "\nstate-5:");
    syscall(SYS_show_buf);

    while(1);
    return 0;
}