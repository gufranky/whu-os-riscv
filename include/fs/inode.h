#ifndef __INODE_H__
#define __INODE_H__

#include "lib/lock.h"

#define INODE_ROOT       0                              // 根节点的inode_num
#define INODE_DISK_SIZE  64                             // 磁盘里inode的大小
#define INODE_PER_BLOCK  (BLOCK_SIZE / INODE_DISK_SIZE) // 每个block里的inode数量

// addrs相关字段
#define N_ADDRS_1   10  // 管理 10 * BLOCK_SIZE = 10KB
#define N_ADDRS_2   2   // 管理 2 * (BLOCK_SIZE / 4) * BLOCK_SIZE = 512KB
#define N_ADDRS_3   1   // 管理 (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4) * BLOCK_SIZE = 64MB
#define N_ADDRS     (N_ADDRS_1 + N_ADDRS_2 + N_ADDRS_3)

// 每个block里面有多少个存储下一级block_num的entry
#define ENTRY_PER_BLOCK (BLOCK_SIZE / sizeof(uint32))

// addrs字段可以容纳的最大空间（单个inode可以管理的最大空间）
// 由于磁盘大小限制, 事实上达不到这个大小
#define INODE_MAXSIZE ((N_ADDRS_1 + N_ADDRS_2 * ENTRY_PER_BLOCK + N_ADDRS_3 * ENTRY_PER_BLOCK * ENTRY_PER_BLOCK) * BLOCK_SIZE)

// type 选项
#define FT_UNUSED 0
#define FT_DIR    1
#define FT_FILE   2
#define FT_DEVICE 3

// inode_num 无效的inode号
#define INODE_NUM_UNUSED 0xFFFF

typedef struct inode {
    // 磁盘里的inode信息 (由slk保护)
    uint16 type;                // inode 管理的文件类型
    uint16 major;               // 设备文件使用: 主设备号
    uint16 minor;               // 设备文件使用: 次设备号
    uint16 nlink;               // 链接数量 (nlink个文件名链接到这个inode)
    uint32 size;                // 文件大小 (字节)
    uint32 addrs[N_ADDRS];      // 文件存储在哪些block里 (分为一级 二级 三级)

    // 内存里的inode信息
    uint16 inode_num;           // inode序号
    uint32 ref;                 // 引用数 (由lk_icache保护)
    bool valid;                 // 上述磁盘里inode字段的有效性 (由slk保护)
    spinlock_t slk;            // 睡眠锁

} inode_t;

// inode 元数据

void     inode_init();                        // 初始化
void     inode_rw(inode_t* ip, bool write);   // 读写inode元数据
inode_t* inode_alloc(uint16 inode_num);       // 在内存申请或查询inode(ref++)
inode_t* inode_create(uint16 type, uint16 major, uint16 minor); // 在磁盘里创建新的inode并在内存申请对应副本
void     inode_free(inode_t* ip);             // 释放inode(ref--) 适时销毁
inode_t* inode_dup(inode_t* ip);              // ref++
void     inode_lock(inode_t* ip);             // 上锁 (valid = false 则从磁盘读入inode)
void     inode_unlock(inode_t* ip);           // 解锁
void     inode_unlock_free(inode_t* ip);      // 解锁 + 释放

// inode 管理的数据

uint32   inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user);
uint32   inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user);
void     inode_free_data(inode_t* ip);

// for debug

void     inode_print(inode_t* ip);

#endif