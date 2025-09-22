#ifndef __PMEM_H__
#define __PMEM_H__

#include "common.h"
#include "lib/lock.h"
#include "lib/print.h"
#define KERNEL_PAGES 2048
// 来自kernel.ld
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
extern char ALLOC_END[];

void  pmem_init(void);
void* pmem_alloc(bool in_kernel);
void  pmem_free(uint64 page, bool in_kernel);
void  memset(void* dst, int val, uint64 len);
typedef struct page_node { 
struct page_node* next;
 } page_node_t;
typedef struct alloc_region { 
uint64 begin; // 起始物理地址
 uint64 end; // 终止物理地址
 spinlock_t lk; // 自旋锁(保护下面两个变量)
 uint32 allocable; // 可分配页面数
 page_node_t list_head; // 可分配链的链头节点 
} alloc_region_t; 
extern alloc_region_t kern_region, user_region; 
#endif