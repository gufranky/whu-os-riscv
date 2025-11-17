#include "lib/print.h"
#include "proc/cpu.h"
//#include "mem/mmap.h"
#include "mem/vmem.h"
#include "syscall/syscall.h"
#include "syscall/sysnum.h"
#include "syscall/sysfunc.h"

// 系统调用跳转
/*static uint64 (*syscalls[])(void) = {
    [SYS_brk]           sys_brk,
    [SYS_mmap]          sys_mmap,
    [SYS_munmap]        sys_munmap,
    [SYS_copyin]        sys_copyin,
    [SYS_copyout]       sys_copyout,
    [SYS_copyinstr]     sys_copyinstr,
};*/

// 系统调用
void syscall()
{
    proc_t* p = myproc();
    uint64 num = p->tf->a7; // 系统调用号存储在a7寄存器中
    uint64 ret = 0;

    switch(num) {
        case SYS_print: // 0号系统调用：输出调用进程的pid
            ret = sys_print();
            break;
        case SYS_brk: // 1号系统调用：内存分配
            ret = sys_brk();
            break;
        case SYS_mmap: // 2号系统调用：内存映射
            ret = sys_mmap();
            break;
        case SYS_munmap: // 3号系统调用：取消内存映射
            ret = sys_munmap();
            break;
        case SYS_fork: // 4号系统调用：创建进程
            ret = sys_fork();
            break;
        case SYS_wait: // 5号系统调用：等待子进程
            ret = sys_wait();
            break;
        case SYS_exit: // 6号系统调用：进程退出
            ret = sys_exit();
            break;
        case SYS_sleep: // 7号系统调用：进程睡眠
            ret = sys_sleep();
            break;
        default:
            printf("unknown sys call %d\n", num);
            ret = -1;
            break;
    }

    // 将返回值写入a0寄存器
    p->tf->a0 = ret;
}

/*
    其他用于读取传入参数的函数
    参数分为两种,第一种是数据本身,第二种是指针
    第一种使用tf->ax传递
    第二种使用uvm_copyin 和 uvm_copyinstr 进行传递
*/

// 读取 n 号参数,它放在 an 寄存器中
static uint64 arg_raw(int n)
{   
    proc_t* proc = myproc();
    switch(n) {
        case 0:
            return proc->tf->a0;
        case 1:
            return proc->tf->a1;
        case 2:
            return proc->tf->a2;
        case 3:
            return proc->tf->a3;
        case 4:
            return proc->tf->a4;
        case 5:        
            return proc->tf->a5;
        default:
            panic("arg_raw: illegal arg num");
            return -1;
    }
}

// 读取 n 号参数, 作为 uint32 存储
void arg_uint32(int n, uint32* ip)
{
    *ip = arg_raw(n);
}

// 读取 n 号参数, 作为 uint64 存储
void arg_uint64(int n, uint64* ip)
{
    *ip = arg_raw(n);
}

// 读取 n 号参数指向的字符串到 buf, 字符串最大长度是 maxlen
void arg_str(int n, char* buf, int maxlen)
{
    proc_t* p = myproc();
    uint64 addr;
    arg_uint64(n, &addr);

    uvm_copyin_str(p->pgtbl, (uint64)buf, addr, maxlen);
}