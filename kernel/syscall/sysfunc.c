#include "proc/cpu.h"
#include "proc/proc.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
//#include "mem/mmap.h"
//#include "lib/str.h"
#include "lib/print.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    proc_t* p = myproc();
    uint64 new_brk;
    arg_uint64(0, &new_brk);

    printf("[sys_brk] proc %d: current heap_top=%p, requested=%p\n",
           p->pid, p->heap_top, new_brk);

    // 如果参数为0，返回当前堆顶地址
    if(new_brk == 0) {
        printf("[sys_brk] proc %d: query mode, returning current heap_top=%p\n",
               p->pid, p->heap_top);
        return p->heap_top;
    }

    // 检查新的堆顶地址是否合理
    uint64 old_heap_top = p->heap_top;

    if(new_brk > old_heap_top) {
        // 堆扩展
        uint64 grow_size = new_brk - old_heap_top;
        printf("[sys_brk] proc %d: expanding heap by %d bytes\n", p->pid, grow_size);

        uint64 new_heap_top = uvm_heap_grow(p->pgtbl, old_heap_top, grow_size);

        if(new_heap_top != new_brk) {
            // 扩展失败
            printf("[sys_brk] proc %d: heap expansion failed\n", p->pid);
            return -1;
        }

        p->heap_top = new_heap_top;
        printf("[sys_brk] proc %d: heap expanded successfully, new_heap_top=%p\n",
               p->pid, new_heap_top);
        return new_heap_top;
    } else if(new_brk < old_heap_top) {
        // 堆收缩
        uint64 shrink_size = old_heap_top - new_brk;
        printf("[sys_brk] proc %d: shrinking heap by %d bytes\n", p->pid, shrink_size);

        uint64 new_heap_top = uvm_heap_ungrow(p->pgtbl, old_heap_top, shrink_size);

        p->heap_top = new_heap_top;
        printf("[sys_brk] proc %d: heap shrunk successfully, new_heap_top=%p\n",
               p->pid, new_heap_top);
        return new_heap_top;
    } else {
        // 堆顶地址不变
        printf("[sys_brk] proc %d: heap_top unchanged=%p\n", p->pid, old_heap_top);
        return old_heap_top;
    }
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    return 0;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    return 0;
}

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}

uint64 sys_print()
{
    char buffer[256]; // 字符串缓冲区，限制最大长度为255字符

    // 获取用户传入的字符串指针（第一个参数）
    arg_str(0, buffer, sizeof(buffer));

    // 打印字符串到控制台
    printf("%s", buffer);

    // 返回打印的字符数（简化实现，返回字符串长度）
    int len = 0;
    while (buffer[len] != '\0' && len < sizeof(buffer) - 1) {
        len++;
    }

    return len;
}
uint64 sys_wait()
{
    uint64 status_addr;

    // 获取状态指针参数（第一个参数）
    arg_uint64(0, &status_addr);

    printf("[sys_wait] proc %d: waiting for child process, status_addr=%p\n",
           myproc()->pid, status_addr);

    // 调用内核的 proc_wait 函数
    int child_pid = proc_wait(status_addr);

    if (child_pid > 0) {
        printf("[sys_wait] proc %d: child process %d exited\n",
               myproc()->pid, child_pid);
        return child_pid;
    } else {
        printf("[sys_wait] proc %d: no child processes\n", myproc()->pid);
        return -1;
    }
}
uint64 sys_exit()
{
    int exit_status;

    // 获取退出状态参数（第一个参数）
    arg_uint32(0, (uint32*)&exit_status);

    printf("[sys_exit] proc %d: exiting with status %d\n", myproc()->pid, exit_status);

    // 调用进程退出函数，这个函数不会返回
    proc_exit(exit_status);

    // 永远不会到达这里
    return 0;
}
uint64 sys_sleep()
{
    uint32 seconds;

    // 获取睡眠时间参数（第一个参数，以秒为单位）
    arg_uint32(0, &seconds);

    printf("[sys_sleep] proc %d: sleeping for %d seconds\n", myproc()->pid, seconds);

    // 简化实现：通过调用 proc_yield() 多次来模拟睡眠
    // 在实际系统中，这里应该使用定时器中断来精确控制睡眠时间
    for (uint32 i = 0; i < seconds; i++) {
        // 让出CPU，等待调度
        proc_yield();
    }

    printf("[sys_sleep] proc %d: woke up after sleeping\n", myproc()->pid);
    return 0;
}
uint64 sys_fork()
{
    printf("[sys_fork] proc %d: creating child process\n", myproc()->pid);

    // 调用内核的 proc_fork 函数创建子进程
    int child_pid = proc_fork();

    if (child_pid > 0) {
        // 父进程：返回子进程PID
        printf("[sys_fork] proc %d: created child process %d\n", myproc()->pid, child_pid);
        return child_pid;
    } else if (child_pid == 0) {
        // 子进程：返回0
        printf("[sys_fork] proc %d: I am the child process\n", myproc()->pid);
        return 0;
    } else {
        // 创建失败：返回-1
        printf("[sys_fork] proc %d: fork failed\n", myproc()->pid);
        return -1;
    }
}