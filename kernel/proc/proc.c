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


// 第一个进程
static proc_t proczero;

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
    proc_t* p = &proczero;

    // pid 设置
    p->pid = 1;

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

    // context 的 ra 设置为 trap_user_return，swtch 恢复时会跳转到这里
    p->ctx.ra = (uint64)trap_user_return;

    // context 的 sp 设置为内核栈顶
    p->ctx.sp = p->kstack + PGSIZE;

    // 上下文切换
    // 设置当前 CPU 的运行进程为 proczero
    mycpu()->proc = p;

    // 使用 swtch 进行上下文切换，从当前 CPU 的 context 切换到 proczero 的 context
    // 这将导致执行流跳转到 trap_user_return，然后进入用户态
    swtch(&mycpu()->ctx, &p->ctx);
}