#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "lib/print.h"
#include "lib/str.h"

extern super_block_t sb;

// 内存中的inode资源 + 保护它的锁
#define N_INODE 32
static inode_t icache[N_INODE];
static spinlock_t lk_icache;

// icache初始化
void inode_init()
{
    spinlock_init(&lk_icache, "icache");

    // 初始化所有inode
    for(int i = 0; i < N_INODE; i++) {
        icache[i].inode_num = INODE_NUM_UNUSED;
        icache[i].ref = 0;
        icache[i].valid = false;
        spinlock_init(&icache[i].slk, "inode");
    }
}

/*---------------------- 与inode本身相关 -------------------*/

// 使用磁盘里的inode更新内存里的inode (write = false)
// 或 使用内存里的inode更新磁盘里的inode (write = true)
// 调用者需要设置inode_num并持有睡眠锁
void inode_rw(inode_t* ip, bool write)
{
    assert(spinlock_holding(&ip->slk), "inode_rw: no lock");

    // 计算inode在磁盘上的位置
    uint32 block_num = sb.inode_start + ip->inode_num / INODE_PER_BLOCK;
    uint32 offset = (ip->inode_num % INODE_PER_BLOCK) * INODE_DISK_SIZE;

    buf_t* buf = buf_read(block_num);

    if(write) {
        // 写入磁盘
        memmove(buf->data + offset, ip, INODE_DISK_SIZE);
        buf_write(buf);
    } else {
        // 从磁盘读取
        memmove(ip, buf->data + offset, INODE_DISK_SIZE);
        ip->valid = true;
    }

    buf_release(buf);
}

// 在icache里查询inode
// 如果没有查询到则申请一个空闲inode
// 如果icache没有空闲inode则报错
// 注意: 获得的inode没有上锁
inode_t* inode_alloc(uint16 inode_num)
{
    spinlock_acquire(&lk_icache);

    // 1. 先查找是否已经在缓存中
    for(int i = 0; i < N_INODE; i++) {
        if(icache[i].inode_num == inode_num) {
            icache[i].ref++;
            spinlock_release(&lk_icache);
            return &icache[i];
        }
    }

    // 2. 没找到，分配一个空闲的inode
    for(int i = 0; i < N_INODE; i++) {
        if(icache[i].ref == 0) {
            icache[i].inode_num = inode_num;
            icache[i].ref = 1;
            icache[i].valid = false;
            spinlock_release(&lk_icache);
            return &icache[i];
        }
    }

    panic("inode_alloc: no available inode");
    return NULL;
}

// 在磁盘里申请一个inode (操作bitmap, 返回inode_num)
// 向icache申请一个inode数据结构
// 填写内存里的inode并以此更新磁盘里的inode
// 注意: 获得的inode没有上锁
inode_t* inode_create(uint16 type, uint16 major, uint16 minor)
{
    // 1. 从bitmap分配一个inode号
    uint16 inode_num = bitmap_alloc_inode();
    if(inode_num == 0) {
        return NULL; // 分配失败
    }

    // 2. 在内存中分配inode结构
    inode_t* ip = inode_alloc(inode_num);

    // 3. 初始化inode
    inode_lock(ip);
    ip->type = type;
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    ip->size = 0;
    memset(ip->addrs, 0, sizeof(ip->addrs));

    // 4. 写入磁盘
    inode_rw(ip, true);
    inode_unlock(ip);

    return ip;
}

// 供inode_free调用
// 在磁盘上删除一个inode及其管理的文件 (修改inode bitmap + block bitmap)
// 调用者需要持有lk_icache, 但不应该持有slk
static void inode_destroy(inode_t* ip)
{
    assert(spinlock_holding(&lk_icache), "inode_destroy: no lk_icache");

    inode_lock(ip);

    // 释放inode管理的所有数据块
    inode_free_data(ip);

    // 清空inode内容
    ip->type = FT_UNUSED;
    ip->major = 0;
    ip->minor = 0;
    ip->nlink = 0;
    ip->size = 0;
    memset(ip->addrs, 0, sizeof(ip->addrs));

    // 写入磁盘
    inode_rw(ip, true);

    // 释放inode bitmap中的位
    bitmap_free_inode(ip->inode_num);

    inode_unlock(ip);
}

// 向icache里归还inode
// inode->ref--
// 调用者不应该持有slk
void inode_free(inode_t* ip)
{
    spinlock_acquire(&lk_icache);

    ip->ref--;

    // 如果引用计数为0且nlink也为0，则销毁inode
    if(ip->ref == 0 && ip->nlink == 0) {
        inode_destroy(ip);
        ip->inode_num = INODE_NUM_UNUSED;
        ip->valid = false;
    }

    spinlock_release(&lk_icache);
}

// ip->ref++ with lock
inode_t* inode_dup(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    ip->ref++;
    spinlock_release(&lk_icache);
    return ip;
}

// 给inode上锁
// 如果valid失效则从磁盘中读入
void inode_lock(inode_t* ip)
{
    assert(ip != NULL, "inode_lock: ip is NULL");
    assert(ip->ref >= 1, "inode_lock: ref < 1");

    spinlock_acquire(&ip->slk);

    if(!ip->valid) {
        inode_rw(ip, false); // 从磁盘读取
    }
}

// 给inode解锁
void inode_unlock(inode_t* ip)
{
    assert(spinlock_holding(&ip->slk), "inode_unlock: no lock");
    spinlock_release(&ip->slk);
}

// 连招: 解锁 + 释放
void inode_unlock_free(inode_t* ip)
{
    inode_unlock(ip);
    inode_free(ip);
}

/*---------------------------- 与inode管理的data相关 --------------------------*/

// 辅助 inode_locate_block
// 递归查询或创建block
static uint32 locate_block(uint32* entry, uint32 bn, uint32 size)
{
    if(*entry == 0)
        *entry = bitmap_alloc_block();

    if(size == 1)
        return *entry;

    uint32* next_entry;
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    uint32 ret = 0;

    buf_t* buf = buf_read(*entry);
    next_entry = (uint32*)(buf->data) + bn / next_size;
    ret = locate_block(next_entry, next_bn, next_size);
    buf_write(buf);
    buf_release(buf);

    return ret;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
static uint32 inode_locate_block(inode_t* ip, uint32 bn)
{
    uint32* addr;

    // 直接块 (0-9)
    if(bn < N_ADDRS_1) {
        addr = &ip->addrs[bn];
        if(*addr == 0)
            *addr = bitmap_alloc_block();
        return *addr;
    }
    bn -= N_ADDRS_1;

    // 一级间接块 (10-11)
    if(bn < N_ADDRS_2 * ENTRY_PER_BLOCK) {
        addr = &ip->addrs[N_ADDRS_1 + bn / ENTRY_PER_BLOCK];
        return locate_block(addr, bn % ENTRY_PER_BLOCK, ENTRY_PER_BLOCK);
    }
    bn -= N_ADDRS_2 * ENTRY_PER_BLOCK;

    // 二级间接块 (12)
    if(bn < N_ADDRS_3 * ENTRY_PER_BLOCK * ENTRY_PER_BLOCK) {
        addr = &ip->addrs[N_ADDRS_1 + N_ADDRS_2];
        return locate_block(addr, bn, ENTRY_PER_BLOCK * ENTRY_PER_BLOCK);
    }

    panic("inode_locate_block: bn too large");
    return 0;
}

// 读取 inode 管理的 data block
// 调用者需要持有 inode 锁
// 成功返回读出的字节数, 失败返回0
uint32 inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user)
{
    assert(spinlock_holding(&ip->slk), "inode_read_data: no lock");

    if(offset >= ip->size)
        return 0;

    if(offset + len > ip->size)
        len = ip->size - offset;

    uint32 total = 0;
    char* dst_ptr = (char*)dst;

    while(total < len) {
        uint32 block_offset = offset % BLOCK_SIZE;
        uint32 block_num = inode_locate_block(ip, offset / BLOCK_SIZE);
        uint32 to_read = BLOCK_SIZE - block_offset;

        if(to_read > len - total)
            to_read = len - total;

        buf_t* buf = buf_read(block_num);

        if(user) {
            // 复制到用户空间（这里简化处理，实际应该使用copyout）
            memmove(dst_ptr + total, buf->data + block_offset, to_read);
        } else {
            memmove(dst_ptr + total, buf->data + block_offset, to_read);
        }

        buf_release(buf);

        total += to_read;
        offset += to_read;
    }

    return total;
}

// 写入 inode 管理的 data block (可能导致管理的 block 增加)
// 调用者需要持有 inode 锁
// 成功返回写入的字节数, 失败返回0
uint32 inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user)
{
    assert(spinlock_holding(&ip->slk), "inode_write_data: no lock");

    if(offset + len > INODE_MAXSIZE)
        return 0; // 超出最大文件大小

    uint32 total = 0;
    char* src_ptr = (char*)src;

    while(total < len) {
        uint32 block_offset = offset % BLOCK_SIZE;
        uint32 block_num = inode_locate_block(ip, offset / BLOCK_SIZE);
        uint32 to_write = BLOCK_SIZE - block_offset;

        if(to_write > len - total)
            to_write = len - total;

        buf_t* buf = buf_read(block_num);

        if(user) {
            // 从用户空间复制（这里简化处理，实际应该使用copyin）
            memmove(buf->data + block_offset, src_ptr + total, to_write);
        } else {
            memmove(buf->data + block_offset, src_ptr + total, to_write);
        }

        buf_write(buf);
        buf_release(buf);

        total += to_write;
        offset += to_write;
    }

    // 更新文件大小
    if(offset > ip->size) {
        ip->size = offset;
        inode_rw(ip, true); // 更新磁盘上的inode
    }

    return total;
}

// 辅助 inode_free_data 做递归释放
static void data_free(uint32 block_num, uint32 level)
{
    assert(block_num != 0, "data_free: block_num = 0");

    // block_num 是 data block
    if(level == 0) goto ret;

    // block_num 是 metadata block
    buf_t* buf = buf_read(block_num);
    for(uint32* addr = (uint32*)buf->data; addr < (uint32*)(buf->data + BLOCK_SIZE); addr++)
    {
        if(*addr == 0) break;
        data_free(*addr, level - 1);
    }
    buf_release(buf);

ret:
    bitmap_free_block(block_num);
    return;
}

// 释放inode管理的 data block
// ip->addrs被清空 ip->size置0
// 调用者需要持有slk
void inode_free_data(inode_t* ip)
{
    assert(spinlock_holding(&ip->slk), "inode_free_data: no lock");

    // 释放直接块
    for(int i = 0; i < N_ADDRS_1; i++) {
        if(ip->addrs[i] != 0) {
            data_free(ip->addrs[i], 0);
            ip->addrs[i] = 0;
        }
    }

    // 释放一级间接块
    for(int i = 0; i < N_ADDRS_2; i++) {
        if(ip->addrs[N_ADDRS_1 + i] != 0) {
            data_free(ip->addrs[N_ADDRS_1 + i], 1);
            ip->addrs[N_ADDRS_1 + i] = 0;
        }
    }

    // 释放二级间接块
    if(ip->addrs[N_ADDRS_1 + N_ADDRS_2] != 0) {
        data_free(ip->addrs[N_ADDRS_1 + N_ADDRS_2], 2);
        ip->addrs[N_ADDRS_1 + N_ADDRS_2] = 0;
    }

    ip->size = 0;
}

static char* inode_types[] = {
    "INODE_UNUSED",
    "INODE_DIR",
    "INODE_FILE",
    "INODE_DEVICE",
};

// 输出inode信息
// for dubug
void inode_print(inode_t* ip)
{
    assert(spinlock_holding(&ip->slk), "inode_print: lk");

    printf("\ninode information:\n");
    printf("num = %d, ref = %d, valid = %d\n", ip->inode_num, ip->ref, ip->valid);
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d, addrs =", ip->size);
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}