#include "mem/pmem.h"
alloc_region_t kern_region, user_region;
void pmem_init(void)
{
    spinlock_init(&kern_region.lk, "kern_region");
    spinlock_init(&user_region.lk, "user_region");
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end = (uint64)ALLOC_BEGIN+KERNEL_PAGES*PGSIZE;
    kern_region.allocable = KERNEL_PAGES;
    user_region.begin = (uint64)ALLOC_BEGIN+KERNEL_PAGES*PGSIZE;
    user_region.end = (uint64)ALLOC_END;
    user_region.allocable = (user_region.end - user_region.begin) / PGSIZE;
    // 用page_node_t结构体管理每个物理页
    uint64 page_addr = kern_region.begin;
    for (int i = 0; i < kern_region.allocable; i++) {
        page_node_t* node = (page_node_t*)page_addr;
        if (i == kern_region.allocable - 1) {
            node->next = NULL;
        } else {
            node->next = (page_node_t*)(page_addr + PGSIZE);
        }
        page_addr += PGSIZE;
    }
    kern_region.list_head.next = (page_node_t*)kern_region.begin;

    // user_region同理
    page_addr = user_region.begin;
    for (int i = 0; i < user_region.allocable; i++) {
        page_node_t* node = (page_node_t*)page_addr;
        if (i == user_region.allocable - 1) {
            node->next = NULL;
        } else {
            node->next = (page_node_t*)(page_addr + PGSIZE);
        }
        page_addr += PGSIZE;
    }
    user_region.list_head.next = (page_node_t*)user_region.begin;
}
void* pmem_alloc(bool in_kernel)
{
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    spinlock_acquire(&region->lk);
    if (region->allocable == 0) {
        spinlock_release(&region->lk);
        return NULL;
    }
    page_node_t* node = region->list_head.next;
    region->list_head.next = node->next;
    region->allocable--;
    spinlock_release(&region->lk);
    return (void*)node;
}
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    //if (page < region->begin || page >= region->end || page % PGSIZE != 0) {
    //    panic("pmem_free");
    //}
    spinlock_acquire(&region->lk);
    page_node_t* node = (page_node_t*)page;
    node->next = region->list_head.next;
    region->list_head.next = node;
    region->allocable++;
    spinlock_release(&region->lk);
}
void memset(void* dst, int val, uint64 len)
{
    uint8* p = (uint8*)dst;
    for (uint64 i = 0; i < len; i++) {
        p[i] = (uint8)val;
    }
}