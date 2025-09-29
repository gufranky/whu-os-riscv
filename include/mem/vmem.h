#ifndef __VMEM_H__
#define __VMEM_H__

#include "common.h"
#include "lib/print.h"
/*
    我们使用RISC-V体系结构中的SV39作为虚拟内存的设计规范

    satp寄存器: MODE(4) + ASID(16) + PPN(44)
    MODE控制虚拟内存模式 ASID与Flash刷新有关 PPN存放页表基地址

    基础页面 4KB
    
    VA和PA的构成:
    VA: VPN[2] + VPN[1] + VPN[0] + offset    9 + 9 + 9 + 12 = 39 (使用uint64存储) => 最大虚拟地址为512GB 
    PA: PPN[2] + PPN[1] + PPN[0] + offset   26 + 9 + 9 + 12 = 56 (使用uint64存储)
    
    为什么是 "9" : 4KB / uint64 = 512 = 2^9 所以一个物理页可以存放512个页表项
    我们使用三级页表对应三级VPN, VPN[2]称为顶级页表、VPN[1]称为次级页表、VPN[0]称为低级页表

    PTE定义:
    reserved + PPN[2] + PPN[1] + PPN[0] + RSW + D A G U X W R V  共64bit
       10        26       9        9       2    1 1 1 1 1 1 1 1
    
    需要关注的部分:
    V : valid
    X W R : execute write read (全0意味着这是页表所在的物理页)
    U : 用户态是否可以访问
    PPN区域 : 存放物理页号

*/

// 页表项
typedef uint64 pte_t;

// 顶级页表
typedef uint64* pgtbl_t;

// satp寄存器相关
#define SATP_SV39 (8L << 60)  // MODE = SV39
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12)) // 设置MODE和PPN字段

// 获取虚拟地址中的虚拟页(VPN)信息 占9bit
#define VA_SHIFT(level)         (12 + 9 * (level))
#define VA_TO_VPN(va,level)     ((((uint64)(va)) >> VA_SHIFT(level)) & 0x1FF)

// PA和PTE之间的转换
#define PA_TO_PTE(pa) ((((uint64)(pa)) >> 12) << 10)
#define PTE_TO_PA(pte) (((pte) >> 10) << 12)

// 页面权限控制 
#define PTE_V (1 << 0) // valid
#define PTE_R (1 << 1) // read
#define PTE_W (1 << 2) // write
#define PTE_X (1 << 3) // execute
#define PTE_U (1 << 4) // user
#define PTE_G (1 << 5) // global
#define PTE_A (1 << 6) // accessed
#define PTE_D (1 << 7) // dirty
extern pgtbl_t kernel_pgtbl;
// 检查一个PTE是否属于pgtbl
#define PTE_CHECK(pte) (((pte) & (PTE_R | PTE_W | PTE_X)) == 0)

// 获取低10bit的flag信息
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// 定义一个相当大的VA, 规定所有VA不得大于它
#define VA_MAX (1ul << 38)

void   vm_print(pgtbl_t pgtbl);
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc);
void   vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm);
void   vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit);

void   kvm_init();
void   kvm_inithart();

#endif