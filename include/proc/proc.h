#ifndef __PROC_H__
#define __PROC_H__

#include "lib/lock.h"

// 页表类型定义
typedef uint64* pgtbl_t;

// mmap_region定义
typedef struct mmap_region mmap_region_t;

// context 定义
typedef struct context {
  uint64 ra; // 返回地址
  uint64 sp; // 栈指针

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
} context_t;

// trapframe 定义
typedef struct trapframe {
    /*   0 */ uint64 kernel_satp;   // kernel page table
    /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
    /*  16 */ uint64 kernel_trap;   // usertrap()
    /*  24 */ uint64 epc;           // saved user program counter
    /*  32 */ uint64 kernel_hartid; // saved kernel tp
    
    /*  40 */ uint64 ra;
    /*  48 */ uint64 sp;
    /*  56 */ uint64 gp;
    /*  64 */ uint64 tp;
    /*  72 */ uint64 t0;
    /*  80 */ uint64 t1;
    /*  88 */ uint64 t2;
    /*  96 */ uint64 s0;
    /* 104 */ uint64 s1;
    /* 112 */ uint64 a0;
    /* 120 */ uint64 a1;
    /* 128 */ uint64 a2;
    /* 136 */ uint64 a3;
    /* 144 */ uint64 a4;
    /* 152 */ uint64 a5;
    /* 160 */ uint64 a6;
    /* 168 */ uint64 a7;
    /* 176 */ uint64 s2;
    /* 184 */ uint64 s3;
    /* 192 */ uint64 s4;
    /* 200 */ uint64 s5;
    /* 208 */ uint64 s6;
    /* 216 */ uint64 s7;
    /* 224 */ uint64 s8;
    /* 232 */ uint64 s9;
    /* 240 */ uint64 s10;
    /* 248 */ uint64 s11;
    /* 256 */ uint64 t3;
    /* 264 */ uint64 t4;
    /* 272 */ uint64 t5;
    /* 280 */ uint64 t6;
} trapframe_t;

/* 
    进程状态集合
    可能的进程状态变换：
    UNSED -> RUNNABLE 进程初始化
    RUNNABLE -> RUNNIGN 进程获得CPU使用权
    RUNNING -> RUNNABLE 进程失去CPU使用权
    RUNNING -> SLEEPING 进程睡眠
    SLEEPING -> RUNNABLE 进程苏醒
    RUNNING -> ZOMBIE 进程杀死自己
    RUNNABLE -> ZOMBIE 进程被杀死
    ZOMBIE -> UNUSED 进程被父进程释放回收
*/
enum proc_state {
    UNUSED,       // 未被使用
    RUNNABLE,     // 准备就绪
    RUNNING,      // 运行中
    SLEEPING,     // 睡眠等待
    ZOMBIE,       // 濒临死亡
};

// 进程定义
typedef struct proc {
    
    spinlock_t lk;           // 自旋锁

    /* 下面的六个字段需要持有锁才能修改 */

    int pid;                 // 标识符
    enum proc_state state;   // 进程状态
    struct proc* parent;     // 父进程
    int exit_state;          // 进程退出时的状态(父进程可能关心)
    void* sleep_space;       // 睡眠是为在等待什么

    pgtbl_t pgtbl;           // 用户态页表
    uint64 heap_top;         // 用户堆顶(以字节为单位)
    uint64 ustack_pages;     // 用户栈占用的页面数量
    mmap_region_t* mmap;     // 用户可映射区域的起始节点
    trapframe_t* tf;         // 用户态内核态切换时的运行环境暂存空间

    uint64 kstack;           // 内核栈的虚拟地址
    context_t ctx;           // 内核态进程上下文
} proc_t;

void     proc_init();                                  // 进程模块初始化
void     proc_make_first();                            // 创建第一个进程并切换到它执行
pgtbl_t  proc_pgtbl_init(uint64 trapframe);            // 进程页表的初始化和基本映射
proc_t*  proc_alloc();                                 // 进程申请
void     proc_free(proc_t* p);                         // 进程释放
int      proc_fork();                                  // 复制子进程
int      proc_wait(uint64 addr);                       // 等待子进程退出
void     proc_exit(int exit_state);                    // 进程退出
void     proc_yield();                                 // 进程放弃CPU
void     proc_sleep(void* sleep_space);// 进程睡眠
void     proc_wakeup(void* sleep_space);               // 进程唤醒
void     proc_sched();                                 // 进程切换到调度器
void     proc_scheduler();                             // 调度器
#endif