#ifndef __BLOCK_BUF__
#define __BLOCK_BUF__

#include "lib/lock.h"

typedef struct buf {
    /* 
        睡眠锁: 保护 data[BLOCK_SIZE] + disk
        block_num + buf_ref 由 lk_buf_cache保护
    */
    spinlock_t slk;

    uint32 block_num; // 对应的磁盘block编号
    uint8  data[BLOCK_SIZE]; // block数据的缓存
    
    uint32 buf_ref; // 还有多少处引用没有释放 
    bool disk;      // 在磁盘驱动中使用

} buf_t;

void   buf_init();
buf_t* buf_read(uint32 block_num);
void   buf_write(buf_t* buf);
void   buf_release(buf_t* buf);
void   buf_print();

#endif