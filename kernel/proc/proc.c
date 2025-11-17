#include "lib/print.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "proc/proc.h"
#include "riscv.h"
#define VA_MAX (1ul << 38)   
// 用户虚拟地址空间的布局常量
#define TRAMPOLINE (VA_MAX - PGSIZE)     // trampoline页的虚拟地址
#define TRAPFRAME (TRAMPOLINE - PGSIZE)  // trapframe页的虚拟地址

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();

// 内存操作函数
void* memcpy(void* dst, const void* src, uint64 len)
{
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (uint64 i = 0; i < len; i++) {
        d[i] = s[i];
    }
    return dst;
}
static proc_t procs[NPROC];

// 第一个进程的指针
static proc_t* proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;


// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    trap_user_return();
}

// 进程模块初始化
void proc_init()
{
    // 初始化 pid 分配锁
    spinlock_init(&lk_pid, "pid");

    // 初始化进程表中的所有进程
    for (int i = 0; i < NPROC; i++) {
        spinlock_init(&procs[i].lk, "proc");

        // 初始化进程状态为未使用
        procs[i].state = UNUSED;
        procs[i].pid = 0;
        procs[i].parent = NULL;
        procs[i].exit_state = 0;
        procs[i].sleep_space = NULL;
        procs[i].pgtbl = NULL;
        procs[i].tf = NULL;
        procs[i].kstack = 0;
        procs[i].heap_top = 0;
        procs[i].ustack_pages = 0;
        procs[i].mmap = NULL;
    }
}

// 进程申请 - 基于xv6的allocproc实现
proc_t* proc_alloc()
{
    proc_t* p;

    for (p = procs; p < &procs[NPROC]; p++) {
        spinlock_acquire(&p->lk);
        if (p->state == UNUSED) {
            goto found;
        } else {
            spinlock_release(&p->lk);
        }
    }
    return NULL;

found:
    p->pid = alloc_pid();
    p->state = RUNNABLE;

    // 分配 trapframe 页面
    if ((p->tf = (trapframe_t*)pmem_alloc(true)) == NULL) {
        proc_free(p);
        spinlock_release(&p->lk);
        return NULL;
    }

    // 创建用户页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (p->pgtbl == NULL) {
        proc_free(p);
        spinlock_release(&p->lk);
        return NULL;
    }

    // 设置内核栈（在procinit中已经预分配）
    p->kstack = KSTACK((int)(p - procs));

    // 设置新的上下文，从fork_return开始执行
    memset(&p->ctx, 0, sizeof(p->ctx));
    p->ctx.ra = (uint64)fork_return;
    p->ctx.sp = p->kstack + PGSIZE;

    return p;
}

// 进程释放 - 基于xv6的freeproc实现
void proc_free(proc_t* p)
{
    if (p->tf)
        pmem_free((uint64)p->tf, true);
    p->tf = NULL;
    if (p->pgtbl)
        uvm_destroy_pgtbl(p->pgtbl, 3); // 用户页表是3级页表
    p->pgtbl = NULL;
    p->heap_top = 0;
    p->pid = 0;
    p->parent = NULL;
    // TODO: 实现进程名
    // p->name[0] = 0;
    p->sleep_space = NULL;
    // TODO: 实现killed字段
    // p->killed = 0;
    p->exit_state = 0;
    p->state = UNUSED;
}





// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 分配并初始化用户页表
    pgtbl_t pgtbl = (pgtbl_t)pmem_alloc(true);
    if (!pgtbl) return NULL;
    memset(pgtbl, 0, PGSIZE);

    // 映射 trampoline 页到最高虚拟地址，与内核页表相同位置
    // 这样用户态和内核态切换时能访问相同的 trampoline 代码
    extern char trampoline[];
    vm_mappages(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 映射 trapframe 页到 trampoline 下方一页
    // trapframe 用于保存用户态寄存器状态
    vm_mappages(pgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W);

    return pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_first()
{
    // 使用 procs[0] 作为第一个进程
    proc_t* p = &procs[0];
    proczero = p;  // 设置 proczero 指向第一个进程

    // 初始化进程锁
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;

    // pid 设置
    p->pid = alloc_pid();

    // 分配 trapframe 页面
    uint64 trapframe_pa = (uint64)pmem_alloc(true);
    if (!trapframe_pa) panic("proc_make_first: failed to allocate trapframe");
    p->tf = (trapframe_t*)trapframe_pa;

    // pagetable 初始化 - 调用 proc_pgtbl_init 完成 trapframe 和 trampoline 的映射
    p->pgtbl = proc_pgtbl_init(trapframe_pa);
    if (!p->pgtbl) panic("proc_make_first: failed to initialize page table");

    // ustack 映射 + 设置 ustack_pages
    uint64 ustack_pa = (uint64)pmem_alloc(true);
    if (!ustack_pa) panic("proc_make_first: failed to allocate user stack");

    // 用户栈映射到 trapframe 下方一页
    uint64 ustack_va = TRAPFRAME - PGSIZE;
    vm_mappages(p->pgtbl, ustack_va, ustack_pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    p->ustack_pages = 1;

    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    uint64 code_pa = (uint64)pmem_alloc(true);
    if (!code_pa) panic("proc_make_first: failed to allocate code page");

    // 代码页映射到虚拟地址 PGSIZE（跳过最低的一页）
    uint64 code_va = PGSIZE;
    vm_mappages(p->pgtbl, code_va, code_pa, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);

    // 将 initcode 数据复制到分配的物理页
    memcpy((void*)code_pa, initcode, initcode_len);

    // 设置 heap_top - 代码页之后就是堆的起始位置
    p->heap_top = code_va + PGSIZE;

    // tf字段设置
    memset(p->tf, 0, sizeof(trapframe_t));

    // 设置用户程序入口点 - 代码从 code_va 开始执行
    p->tf->epc = code_va;

    // 设置用户栈指针 - 栈从高地址往低地址增长，初始指向栈顶
    p->tf->sp = ustack_va + PGSIZE;

    // 设置内核相关字段，这些将在用户态陷入内核时使用
    p->tf->kernel_satp = r_satp();         // 当前内核页表
    p->tf->kernel_hartid = r_tp();         // 当前 CPU ID

    // 内核字段设置
    // 分配内核栈虚拟地址 - 使用 KSTACK 宏为当前CPU分配内核栈
    p->kstack = KSTACK(mycpuid());

    // 设置 trapframe 的内核栈字段
    p->tf->kernel_sp = p->kstack + PGSIZE;     // 内核栈指针指向栈顶

    // 设置进程上下文 - 初始化为零，然后设置关键字段
    memset(&p->ctx, 0, sizeof(context_t));

    // ra (返回地址) 设置为 trap_user_return，进程恢复时将跳转到这里
    p->tf->kernel_trap = (uint64)trap_user_return;

    // context 的 ra 设置为 fork_return，swtch 恢复时会跳转到这里释放锁
    p->ctx.ra = (uint64)fork_return;
    printf("proc_make_first: setting ctx.ra to fork_return (0x%p) for proc %d\n",
           (uint64)fork_return, p->pid);

    // context 的 sp 设置为内核栈顶
    p->ctx.sp = p->kstack + PGSIZE;

    // 上下文切换
    // 设置当前 CPU 的运行进程为 proczero
    mycpu()->proc = p;

    printf("proc_make_first: about to switch to first process (pid=%d)\n", p->pid);

    // 不要直接释放锁，让调度器来处理
    spinlock_release(&p->lk);

    // 通过调度器启动第一个进程
    proc_scheduler();
}

// 进程复制 - 基于xv6实现
int proc_fork()
{
    int pid;
    proc_t* curr = myproc();
    proc_t* child;

    // 分配新进程
    child = proc_alloc();
    if (!child) {
        return -1;
    }

    // 复制父进程的用户内存到子进程
    uvm_copy_pgtbl(curr->pgtbl, child->pgtbl, curr->heap_top, curr->ustack_pages, curr->mmap);
    child->heap_top = curr->heap_top;

    // 复制父进程的 trapframe
    memcpy(child->tf, curr->tf, sizeof(trapframe_t));

    // 子进程的返回值为 0
    child->tf->a0 = 0;

    // TODO: 复制打开的文件描述符
    // for (i = 0; i < NOFILE; i++)
    //     if (curr->ofile[i])
    //         child->ofile[i] = filedup(curr->ofile[i]);
    // child->cwd = idup(curr->cwd);

    // TODO: 复制进程名
    // safestrcpy(child->name, curr->name, sizeof(curr->name));

    pid = child->pid;

    spinlock_release(&child->lk);

    // 在等待锁保护下设置父子关系
    // TODO: 实现wait_lock机制
    // acquire(&wait_lock);
    child->parent = curr;
    // release(&wait_lock);

    // 设置子进程为可运行状态
    spinlock_acquire(&child->lk);
    child->state = RUNNABLE;
    spinlock_release(&child->lk);

    return pid;
}

// 进程放弃CPU的控制权 - 基于xv6的yield实现
void proc_yield()
{
    proc_t* p = myproc();
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched();
    spinlock_release(&p->lk);
}

// 等待一个子进程进入 ZOMBIE 状态 - 基于xv6的kwait实现
int proc_wait(uint64 addr)
{
    proc_t* pp;
    int havekids, pid;
    proc_t* curr = myproc();

    // TODO: 实现wait_lock机制
    // spinlock_acquire(&wait_lock);
    for (;;) {
        // 扫描进程表查找已退出的子进程
        havekids = 0;
        for (pp = procs; pp < &procs[NPROC]; pp++) {
            if (pp->parent == curr) {
                // 确保子进程不再处于exit()或swtch()中
                spinlock_acquire(&pp->lk);

                havekids = 1;
                if (pp->state == ZOMBIE) {
                    // 找到一个已退出的子进程
                    pid = pp->pid;
                    if (addr != 0) {
                        uvm_copyout(curr->pgtbl, addr, (uint64)&pp->exit_state,
                                                sizeof(pp->exit_state));
                    }
                    proc_free(pp);
                    spinlock_release(&pp->lk);
                    // TODO: release(&wait_lock);
                    return pid;
                }
                spinlock_release(&pp->lk);
            }
        }

        // 如果没有子进程或者当前进程被杀死，就不用等待了
        // TODO: 实现killed函数
        // if (!havekids || killed(curr)) {
        if (!havekids) {
            // TODO: release(&wait_lock);
            return -1;
        }

        // 等待子进程退出
        // TODO: 实现wait_lock机制
        // proc_sleep(curr, &wait_lock);
        proc_sleep(curr);
    }
}

// 将父进程的所有子进程转交给init进程 - 基于xv6的reparent实现
// 调用者必须持有wait_lock
static void proc_reparent(proc_t* parent)
{
    proc_t* pp;

    for (pp = procs; pp < &procs[NPROC]; pp++) {
        if (pp->parent == parent) {
            pp->parent = proczero;
            proc_wakeup(proczero);
        }
    }
}

// 唤醒一个进程
static void __attribute__((unused)) proc_wakeup_one(proc_t* p)
{
    assert(spinlock_holding(&p->lk), "proc_wakeup_one: lock");
    if(p->state == SLEEPING && p->sleep_space == p) {
        p->state = RUNNABLE;
    }
}

// 进程退出 - 基于xv6的kexit实现
void proc_exit(int exit_state)
{
    proc_t* curr = myproc();

    if (curr == proczero)
        panic("init exiting");

    // TODO: 关闭所有打开的文件
    // for (int fd = 0; fd < NOFILE; fd++) {
    //     if (curr->ofile[fd]) {
    //         struct file *f = curr->ofile[fd];
    //         fileclose(f);
    //         curr->ofile[fd] = 0;
    //     }
    // }

    // TODO: 文件系统相关清理
    // begin_op();
    // iput(curr->cwd);
    // end_op();
    // curr->cwd = 0;

    // TODO: 实现wait_lock机制
    // spinlock_acquire(&wait_lock);

    // 将所有子进程转交给init进程
    proc_reparent(curr);

    // 唤醒父进程（父进程可能在wait中睡眠）
    proc_wakeup(curr->parent);

    spinlock_acquire(&curr->lk);

    curr->exit_state = exit_state;
    curr->state = ZOMBIE;

    // TODO: release(&wait_lock);

    // 跳转到调度器，永不返回
    proc_sched();
    panic("zombie exit");
}

// 进程切换到调度器 - 基于xv6的sched实现
// 调用者必须持有当前进程的锁并已经改变proc->state
// 保存并恢复intena因为intena是这个内核线程的属性，
// 而不是这个CPU的属性
void proc_sched()
{
    int intena;
    proc_t* p = myproc();

    if (!spinlock_holding(&p->lk))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched RUNNING");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->ctx, &mycpu()->ctx);
    mycpu()->intena = intena;
}

// 调度器 - 基于xv6的scheduler实现
void proc_scheduler()
{
    proc_t* p;
    cpu_t* c = mycpu();

    c->proc = NULL;
    for (;;) {
        // 最近运行的进程可能关闭了中断；启用中断以避免
        // 所有进程都在等待时的死锁。然后再关闭中断
        // 以避免中断和wfi之间可能的竞争。
        intr_on();
        intr_off();

        int found = 0;
        for (p = procs; p < &procs[NPROC]; p++) {
            spinlock_acquire(&p->lk);
            if (p->state == RUNNABLE) {
                // 切换到选中的进程。进程的工作是
                // 释放其锁然后重新获取它
                // 在跳回到我们之前。
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->ctx, &p->ctx);

                // 进程现在运行完毕。
                // 它应该在回来之前改变其p->state。
                c->proc = NULL;
                found = 1;
            }
            spinlock_release(&p->lk);
        }
        if (found == 0) {
            // 没有任何可运行的进程；停止在这个核心上运行直到中断。
            asm volatile("wfi");
        }
    }
}

// 进程睡眠在channel上 - 基于xv6的sleep实现
// 释放条件锁lk，在chan上睡眠
// 被唤醒时重新获取lk
void proc_sleep(void* chan)
{
    proc_t* p = myproc();

    // 必须获取p->lk以便
    // 改变p->state然后调用sched。
    // 一旦我们持有p->lk，我们可以
    // 保证不会错过任何唤醒
    // （wakeup会锁定p->lk），
    // 所以可以安全地释放lk。

    spinlock_acquire(&p->lk);

    // 进入睡眠
    p->sleep_space = chan;
    p->state = SLEEPING;
    proc_sched();

    // 整理
    p->sleep_space = NULL;

    // 重新获取原始锁
    spinlock_release(&p->lk);
}

// 唤醒所有在channel上睡眠的进程 - 基于xv6的wakeup实现
// 调用者应该持有条件锁
void proc_wakeup(void* chan)
{
    proc_t* p;

    for (p = procs; p < &procs[NPROC]; p++) {
        if (p != myproc()) {
            spinlock_acquire(&p->lk);
            if (p->state == SLEEPING && p->sleep_space == chan) {
                p->state = RUNNABLE;
            }
            spinlock_release(&p->lk);
        }
    }
}