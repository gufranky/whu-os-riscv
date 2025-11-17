#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"
#include "syscall/syscall.h"

// 用户虚拟地址空间的布局常量
#define VA_MAX (1ul << 38)               // 最大虚拟地址
#define TRAMPOLINE (VA_MAX - PGSIZE)     // trampoline页的虚拟地址
#define TRAPFRAME (TRAMPOLINE - PGSIZE)  // trapframe页的虚拟地址

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern void external_interrupt_handler();
extern void timer_interrupt_handler();

// 异常信息
static char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();

    // 调试信息：显示任何用户态trap
    //printf("USER TRAP: scause=0x%p sepc=0x%p CPU=%d\n", scause, sepc, mycpuid());

    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");

    // 设置内核trap向量，以防在内核中发生trap
    w_stvec((uint64)kernel_vector);

    // 保存用户程序计数器
    p->tf->epc = sepc;

    // 中断异常处理核心逻辑
    if (scause & 0x8000000000000000ULL) {
        // 这是一个中断
        int interrupt_id = scause & 0xf;
        switch (interrupt_id) {
            case 1: // S-mode软件中断（由M-mode时钟中断触发）
                timer_interrupt_handler();
                // 清除S-mode软件中断位
                w_sip(r_sip() & ~2);
                break;
            case 5: // S-mode时钟中断（直接委托的时钟中断）
                timer_interrupt_handler();
                break;
            case 9: // S-mode外设中断
                external_interrupt_handler();
                break;
            default:
                printf("Unknown interrupt in user mode: %d\n", interrupt_id);
                break;
        }
    } else {
        // 这是一个异常
        int exception_id = scause & 0xf;

        switch (exception_id) {
            case 8: // Environment call from U-mode (系统调用)
                // 系统调用处理
                p->tf->epc += 4;          // 系统调用成功后跳过ecall指令（4字节）

                // 调用统一的系统调用处理函数
                syscall();
                break;

            case 12: // Instruction page fault
            case 13: // Load page fault
            case 15: // Store/AMO page fault
                // 页面错误处理
                printf("Page fault in user mode: %s (id=%d)\n",
                       exception_info[exception_id], exception_id);
                printf("sepc=0x%p stval=0x%p\n", sepc, stval);

                assert(0, "Page fault not implemented");
                break;

            default:
                // 其他异常
                printf("Exception in user mode: %s (id=%d)\n",
                       exception_id < 16 ? exception_info[exception_id] : "unknown",
                       exception_id);
                printf("sepc=0x%p stval=0x%p\n", sepc, stval);

                // 对于未处理的异常，终止进程
                assert(0, "Unhandled user exception");
                break;
        }
    }

    // 返回用户态
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t* p = myproc();


    intr_off();

    // 设置用户trap向量
    w_stvec(TRAMPOLINE + (user_vector - trampoline));

    // 设置trapframe的值，这些值将由trampoline.S使用
    p->tf->kernel_satp = r_satp();         // 内核页表
    p->tf->kernel_sp = p->kstack + PGSIZE; // 进程的内核栈
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = r_tp();         // hartid，用于mycpu()

    // 设置S-mode中断
    // 我们希望在用户空间接收定时器中断
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // 清除SPP位以返回用户模式
    x |= SSTATUS_SPIE; // 在用户模式下启用中断
    w_sstatus(x);

    // 设置S-mode异常程序计数器为保存的用户pc
    // 检查 epc 值是否合理
    if (p->tf->epc >= 0x80000000ULL) {
    }
    w_sepc(p->tf->epc);

    // 告诉trampoline.S用户页表切换到哪里
    uint64 satp = MAKE_SATP(p->pgtbl);

    void (*fn)(uint64, uint64) = (void(*)(uint64, uint64))(TRAMPOLINE + (user_return - trampoline));
    fn(TRAPFRAME, satp);
}