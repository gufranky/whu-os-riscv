//#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
//#include "lib/str.h"
#include "memlayout.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
/* static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");

        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        page = (uint64)pmem_alloc(false);
        // memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
} */

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
/* static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");

    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
} */

// 打印以 mmap 为首的 mmap 链
// for debug
/* void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
} */

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3, level = 0 说明是页表管理的物理页
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    // 遍历当前页表的所有项
    for(int i = 0; i < 512; i++) {
        pte_t pte = pgtbl[i];
        if((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // 这是一个指向下级页表的有效页表项
            uint64 child_pa = PTE_TO_PA(pte);
            destroy_pgtbl((pgtbl_t)child_pa, level - 1);
        } else if(pte & PTE_V) {
            // 这是一个指向物理页的有效页表项 (level == 0时)
            uint64 pa = PTE_TO_PA(pte);
            pmem_free(pa, false); // 释放用户物理页
        }
    }
    // 释放当前页表所占用的物理页
    pmem_free((uint64)pgtbl, false);
}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    if(pgtbl == NULL) return;

    // 计算特殊页面的虚拟地址
    uint64 VA_MAX = (1UL << 38);
    uint64 TRAMPOLINE_VA = VA_MAX - PGSIZE;
    uint64 TRAPFRAME_VA = TRAMPOLINE_VA - PGSIZE;

    // 在销毁页表之前，先清除 trampoline 和 trapframe 的映射
    // 但不释放它们的物理页面
    pte_t* trampoline_pte = vm_getpte(pgtbl, TRAMPOLINE_VA, false);
    if(trampoline_pte && (*trampoline_pte & PTE_V)) {
        *trampoline_pte = 0;  // 清除映射，但不释放物理页（因为是共享的）
    }

    pte_t* trapframe_pte = vm_getpte(pgtbl, TRAPFRAME_VA, false);
    if(trapframe_pte && (*trapframe_pte & PTE_V)) {
        *trapframe_pte = 0;   // 清除映射，但不释放物理页（将在proc_free中单独处理）
    }

    // 调用内部的递归释放函数
    destroy_pgtbl(pgtbl, level);
}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top */
    // 用户空间通常从0开始到heap_top
    uint64 USER_BASE = 0;
    for(uint64 va = USER_BASE; va < heap_top; va += PGSIZE) {
        pte_t* pte = vm_getpte(old, va, false);
        if(pte != NULL && (*pte & PTE_V)) {
            uint64 pa = PTE_TO_PA(*pte);
            int flags = PTE_FLAGS(*pte);

            // 分配新的物理页并拷贝内容
            uint64 new_pa = (uint64)pmem_alloc(false);
            if(new_pa == 0) {
                panic("uvm_copy_pgtbl: pmem_alloc failed");
            }

            // 拷贝页面内容
            char* src = (char*)pa;
            char* dst = (char*)new_pa;
            for(int i = 0; i < PGSIZE; i++) {
                dst[i] = src[i];
            }

            // 在新页表中建立映射
            vm_mappages(new, va, new_pa, PGSIZE, flags);
        }
    }

    /* step-2: ustack */
    // 用户栈位于 TRAPFRAME 下方
    // 需要与 proc_make_first 中的映射保持一致
    uint64 VA_MAX = (1UL << 38);
    uint64 TRAMPOLINE = VA_MAX - PGSIZE;
    uint64 TRAPFRAME = TRAMPOLINE - PGSIZE;
    uint64 USTACK_BASE = TRAPFRAME - PGSIZE;
    for(uint32 i = 0; i < ustack_pages; i++) {
        uint64 va = USTACK_BASE - i * PGSIZE;
        pte_t* pte = vm_getpte(old, va, false);
        if(pte != NULL && (*pte & PTE_V)) {
            uint64 pa = PTE_TO_PA(*pte);
            int flags = PTE_FLAGS(*pte);

            // 分配新的物理页并拷贝内容
            uint64 new_pa = (uint64)pmem_alloc(false);
            if(new_pa == 0) {
                panic("uvm_copy_pgtbl: pmem_alloc failed");
            }

            // 拷贝页面内容
            char* src = (char*)pa;
            char* dst = (char*)new_pa;
            for(int i = 0; i < PGSIZE; i++) {
                dst[i] = src[i];
            }

            // 在新页表中建立映射
            vm_mappages(new, va, new_pa, PGSIZE, flags);
        }
    }

    /* step-3: mmap_region */
    // mmap相关的不实现，跳过
}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");

    // 修改 mmap 链 (分情况的链式操作)

    // 修改页表 (物理页申请 + 页表映射)

}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");

    // new mmap_region 的产生

    // 尝试合并 mmap_region

    // 页表释放

}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top + len;

    // 为新增的堆空间分配物理页并建立映射
    // 需要按页对齐
    uint64 old_heap_aligned = (heap_top + PGSIZE - 1) & ~(PGSIZE - 1); // 向上对齐到页边界
    uint64 new_heap_aligned = (new_heap_top + PGSIZE - 1) & ~(PGSIZE - 1);

    for(uint64 va = old_heap_aligned; va < new_heap_aligned; va += PGSIZE) {
        // 分配物理页
        uint64 pa = (uint64)pmem_alloc(false);
        if(pa == 0) {
            // 分配失败，需要回滚已分配的页面
            for(uint64 rollback_va = old_heap_aligned; rollback_va < va; rollback_va += PGSIZE) {
                pte_t* pte = vm_getpte(pgtbl, rollback_va, false);
                if(pte != NULL && (*pte & PTE_V)) {
                    uint64 rollback_pa = PTE_TO_PA(*pte);
                    pmem_free(rollback_pa, false);
                    *pte = 0;
                }
            }
            return heap_top; // 返回原来的heap_top
        }

        // 清零新分配的页面
        memset((void*)pa, 0, PGSIZE);

        // 建立映射，堆空间一般具有读写权限
        vm_mappages(pgtbl, va, pa, PGSIZE, PTE_R | PTE_W | PTE_U);
    }

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    uint64 new_heap_top = heap_top - len;

    // 防止堆顶小于0
    if(new_heap_top > heap_top) { // 考虑无符号整数下溢
        new_heap_top = 0;
    }

    // 释放不再需要的物理页
    // 需要按页对齐
    uint64 new_heap_aligned = (new_heap_top + PGSIZE - 1) & ~(PGSIZE - 1); // 向上对齐到页边界
    uint64 old_heap_aligned = (heap_top + PGSIZE - 1) & ~(PGSIZE - 1);

    for(uint64 va = new_heap_aligned; va < old_heap_aligned; va += PGSIZE) {
        pte_t* pte = vm_getpte(pgtbl, va, false);
        if(pte != NULL && (*pte & PTE_V)) {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, false); // 释放物理页
            *pte = 0; // 清除页表项
        }
    }

    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    if(len == 0) return;

    uint64 src_va = src;
    uint64 dst_addr = dst;
    uint32 copied = 0;

    while(copied < len) {
        // 获取当前用户虚拟地址对应的页表项
        pte_t* pte = vm_getpte(pgtbl, src_va, false);
        if(pte == NULL || !(*pte & PTE_V)) {
            panic("uvm_copyin: invalid virtual address");
            return;
        }

        // 计算物理地址和页内偏移
        uint64 pa = PTE_TO_PA(*pte);
        uint64 page_offset = src_va & (PGSIZE - 1);
        uint64 src_pa = pa + page_offset;

        // 计算本次拷贝的字节数（不能超过页边界）
        uint32 bytes_in_page = PGSIZE - page_offset;
        uint32 bytes_to_copy = (len - copied < bytes_in_page) ? (len - copied) : bytes_in_page;

        // 从用户态物理地址拷贝到内核态地址
        char* src_ptr = (char*)src_pa;
        char* dst_ptr = (char*)dst_addr;
        for(uint32 i = 0; i < bytes_to_copy; i++) {
            dst_ptr[i] = src_ptr[i];
        }

        // 更新指针和计数器
        src_va += bytes_to_copy;
        dst_addr += bytes_to_copy;
        copied += bytes_to_copy;
    }
}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    if(len == 0) return;

    uint64 dst_va = dst;
    uint64 src_addr = src;
    uint32 copied = 0;

    while(copied < len) {
        // 获取当前用户虚拟地址对应的页表项
        pte_t* pte = vm_getpte(pgtbl, dst_va, false);
        if(pte == NULL || !(*pte & PTE_V)) {
            panic("uvm_copyout: invalid virtual address");
            return;
        }

        // 计算物理地址和页内偏移
        uint64 pa = PTE_TO_PA(*pte);
        uint64 page_offset = dst_va & (PGSIZE - 1);
        uint64 dst_pa = pa + page_offset;

        // 计算本次拷贝的字节数（不能超过页边界）
        uint32 bytes_in_page = PGSIZE - page_offset;
        uint32 bytes_to_copy = (len - copied < bytes_in_page) ? (len - copied) : bytes_in_page;

        // 从内核态地址拷贝到用户态物理地址
        char* src_ptr = (char*)src_addr;
        char* dst_ptr = (char*)dst_pa;
        for(uint32 i = 0; i < bytes_to_copy; i++) {
            dst_ptr[i] = src_ptr[i];
        }

        // 更新指针和计数器
        dst_va += bytes_to_copy;
        src_addr += bytes_to_copy;
        copied += bytes_to_copy;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    if(maxlen == 0) return;

    uint64 src_va = src;
    uint64 dst_addr = dst;
    uint32 copied = 0;

    while(copied < maxlen) {
        // 获取当前用户虚拟地址对应的页表项
        pte_t* pte = vm_getpte(pgtbl, src_va, false);
        if(pte == NULL || !(*pte & PTE_V)) {
            panic("uvm_copyin_str: invalid virtual address");
            return;
        }

        // 计算物理地址和页内偏移
        uint64 pa = PTE_TO_PA(*pte);
        uint64 page_offset = src_va & (PGSIZE - 1);
        uint64 src_pa = pa + page_offset;

        // 计算本次拷贝的字节数（不能超过页边界和maxlen）
        uint32 bytes_in_page = PGSIZE - page_offset;
        uint32 remaining = maxlen - copied;
        uint32 bytes_to_check = (remaining < bytes_in_page) ? remaining : bytes_in_page;

        // 从用户态物理地址拷贝到内核态地址，同时检查'\0'
        char* src_ptr = (char*)src_pa;
        char* dst_ptr = (char*)dst_addr;
        uint32 i;
        for(i = 0; i < bytes_to_check; i++) {
            dst_ptr[i] = src_ptr[i];
            if(src_ptr[i] == '\0') {
                // 遇到字符串结束符，返回
                return;
            }
        }

        // 更新指针和计数器
        src_va += i;
        dst_addr += i;
        copied += i;
    }

    // 如果达到maxlen还没有遇到'\0'，手动添加字符串结束符
    if(copied == maxlen) {
        char* dst_ptr = (char*)dst_addr;
        dst_ptr[-1] = '\0'; // 覆盖最后一个字符为'\0'
    }
}