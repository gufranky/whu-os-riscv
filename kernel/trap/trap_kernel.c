#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "memlayout.h"
#include "riscv.h"

// 中断信息
// static char* interrupt_info[16] = {
//     "U-mode software interrupt",      // 0
//     "S-mode software interrupt",      // 1
//     "reserved-1",                     // 2
//     "M-mode software interrupt",      // 3
//     "U-mode timer interrupt",         // 4
//     "S-mode timer interrupt",         // 5
//     "reserved-2",                     // 6
//     "M-mode timer interrupt",         // 7
//     "U-mode external interrupt",      // 8
//     "S-mode external interrupt",      // 9
//     "reserved-3",                     // 10
//     "M-mode external interrupt",      // 11
//     "reserved-4",                     // 12
//     "reserved-5",                     // 13
//     "reserved-6",                     // 14
//     "reserved-7",                     // 15
// };

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

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    // 初始化PLIC
    plic_init();
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    // 设置S-mode中断向量
    w_stvec((uint64)kernel_vector);

    // 初始化PLIC
    plic_inithart();

    // 启用S-mode软件中断、外设中断和定时器中断
    w_sie(r_sie() | SIE_SSIE | SIE_SEIE | SIE_STIE);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    // 获取中断号
    int irq = plic_claim();

    switch (irq) {
        case UART_IRQ:
            // 处理UART中断 - 键盘输入显示
            uart_intr();
            break;
        case 0:
            // 没有中断
            break;
        default:
            printf("Unknown external interrupt: %d\n", irq);
            break;
    }

    // 确认中断处理完成
    if (irq) {
        plic_complete(irq);
    }
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    // 只有CPU 0更新系统时钟，避免双核重复更新
    if (mycpuid() == 0) {
        timer_update();
    }

    // 每个核心都输出自己的时钟中断标识
    int cpuid = mycpuid();
    printf("t%d\n", cpuid);

    // 时间片计算：仅更新当前进程的时间片计数
    proc_t* p = myproc();
    if (p != NULL) {
        spinlock_acquire(&p->lk);
        p->total_time++;
        if (p->time_slice > 0) {
            p->time_slice--;
            // 显示时间片倒计时（仅在快用完时显示，避免输出过多）
            if (p->time_slice <= 3) {
                printf("[TIME] Process %d: %d ticks remaining\n", p->pid, p->time_slice);
            }
        }
        spinlock_release(&p->lk);
    }
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

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
                printf("Unknown interrupt: %d\n", interrupt_id);
                break;
        }
    } else {
        // 这是一个异常
        int exception_id = scause & 0xf;
        printf("Exception occurred: %s (id=%d)\n",
               exception_id < 16 ? exception_info[exception_id] : "unknown",
               exception_id);
        printf("sepc=0x%p stval=0x%p\n", sepc, stval);

        // 对于未处理的异常，可以选择panic
        assert(0, "Unhandled exception");
    }

    // 检查时间片是否用完，如果用完则进行调度（仅对时钟中断）
    if (scause & 0x8000000000000000ULL) {
        int interrupt_id = scause & 0xf;
        if (interrupt_id == 1 || interrupt_id == 5) { // 时钟中断
            proc_t* p = myproc();
            if (p != NULL) {
                spinlock_acquire(&p->lk);
                if (p->time_slice == 0) {
                    // 时间片用完，重置时间片并触发调度
                    printf("[SCHED-K] Process %d time slice expired in kernel mode, switching...\n", p->pid);
                    p->time_slice = TIME_SLICE;
                    p->state = RUNNABLE;
                    proc_sched();
                    // proc_sched会释放锁并切换到调度器，不会返回到这里
                    // 当进程再次被调度时会从这里继续执行
                    printf("[SCHED-K] Process %d resumed after kernel scheduling\n", p->pid);
                }
                spinlock_release(&p->lk);
            }
        }
    }
}