#include "mem/vmem.h"
#include "mem/pmem.h"
#include "common.h"
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    // 检查虚拟地址是否合法
    if (va >= VA_MAX)
        panic("vm_getpte: va out of range");

    // 三级页表遍历
    for (int level = 2; level > 0; level--) {
        int idx = VA_TO_VPN(va, level); // 获取当前级别的VPN
        pte_t* pte = &pgtbl[idx];
        if (*pte & PTE_V) {
            // 有效，跳转到下一级页表
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        } else {
            // 无效，若alloc为真则分配新页表
            if (!alloc)
                return NULL;
            pgtbl_t newtbl = (pgtbl_t)pmem_alloc(true);
            memset(newtbl, 0, PGSIZE);
            *pte = PA_TO_PTE(newtbl) | PTE_V;
            pgtbl = newtbl;
        }
    }
    // 最低级页表项
    int idx = VA_TO_VPN(va, 0);
    return &pgtbl[idx];
}
// 将虚拟地址va开始的len字节映射到物理地址pa，权限为perm
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    uint64 start = va;
    uint64 end = va + len;
    for (; start < end; start += PGSIZE, pa += PGSIZE) {
        pte_t* pte = vm_getpte(pgtbl, start, true);
        if (!pte)
            panic("vm_mappages: getpte fail");
        if (*pte & PTE_V)
            panic("vm_mappages: remap"); // 不允许重复映射
        *pte = PA_TO_PTE(pa) | perm | PTE_V;
    }
}
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    uint64 start = va;
    uint64 end = va + len;
    for (; start < end; start += PGSIZE) {
        pte_t* pte = vm_getpte(pgtbl, start, false);
        if (!pte || !(*pte & PTE_V))
            panic("vm_unmappages: not mapped");
        if (freeit) {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, true);
        }
        *pte = 0; // 清除页表项
    }
}

#include "memlayout.h"

// 内核页表全局变量
pgtbl_t kernel_pgtbl = NULL;

// 权限宏
#define KERN_PERM (PTE_R | PTE_W | PTE_X)
#define MEM_START   0x80000000UL
#define MEM_END     0x88000000UL

// 内核页表初始化：对硬件寄存器区和内存区做恒等映射
void kvm_init() {
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    memset(kernel_pgtbl, 0, PGSIZE);
    // 1. 硬件寄存器区（QEMU保留区）恒等映射
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE+1000, 1000, KERN_PERM);
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE+0x200000, 0x200000, KERN_PERM);
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE+0x10000, 0x10000, KERN_PERM);
    // 2. 可用内存区 0x80000000~0x88000000 恒等映射
    vm_mappages(kernel_pgtbl, MEM_START, MEM_START, MEM_END - MEM_START, KERN_PERM);
}

// 启用内核页表
void kvm_inithart() {
    if (!kernel_pgtbl) panic("kvm_inithart: kernel_pgtbl null");
    uint64 satp = MAKE_SATP(kernel_pgtbl);
    asm volatile("csrw satp, %0" :: "r"(satp));
    asm volatile("sfence.vma");
}