#include "fs/buf.h"
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/str.h"
#include "proc/proc.h"

#define N_BLOCK_BUF 64
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

// 将buf包装成双向循环链表的node
typedef struct buf_node {
    buf_t buf;
    struct buf_node* next;
    struct buf_node* prev;
} buf_node_t;

// buf cache
buf_node_t buf_cache[N_BLOCK_BUF];
static buf_node_t head_buf; // ->next 已分配 ->prev 可分配
static spinlock_t lk_buf_cache; // 这个锁负责保护 链式结构 + buf_ref + block_num

// 链表操作
static void insert_head(buf_node_t* buf_node, bool head_next)
{
    // 离开原位置
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入新位置
    if(head_next) { // 插入 head->next (已分配列表)
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;
    } else { // 插入 head->prev (可分配列表)
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

// 初始化
void buf_init()
{
    spinlock_init(&lk_buf_cache, "buf_cache");
    // 初始化头结点，构建双向循环链表
    head_buf.next = &head_buf;
    head_buf.prev = &head_buf;

    // 将所有buf节点初始化并加入可分配列表
    for(int i = 0; i < N_BLOCK_BUF; i++) {
        buf_cache[i].buf.block_num = BLOCK_NUM_UNUSED;
        buf_cache[i].buf.buf_ref = 0;
        buf_cache[i].buf.disk = false;
        spinlock_init(&buf_cache[i].buf.slk, "buf");
        memset(buf_cache[i].buf.data, 0, BLOCK_SIZE);

        // 加入可分配列表(head->prev)
        insert_head(&buf_cache[i], false);
    }
}

/*
    首先假设这个block_num对应的block在内存中有备份, 找到它并上锁返回
    如果找不到, 尝试申请一个无人使用的buf, 去磁盘读取对应block并上锁返回
    如果没有空闲buf, panic报错
    (建议合并xv6的bget())
*/
buf_t* buf_read(uint32 block_num)
{
    spinlock_acquire(&lk_buf_cache);
    // 1. 先在已分配的buf中查找
    buf_node_t* node = head_buf.next;
    while(node != &head_buf) {
        if(node->buf.block_num == block_num) {
            node->buf.buf_ref++;
            spinlock_release(&lk_buf_cache);
            spinlock_acquire(&node->buf.slk);
            return &node->buf;
        }
        node = node->next;
    }
    // 2. 没找到，从可分配列表中获取一个buf
    if(head_buf.prev == &head_buf) {
        panic("buf_read: no available buffer");
    }

    buf_node_t* buf_node = head_buf.prev;
    buf_t* buf = &buf_node->buf;

    // 确保这个buf没有被引用
    if(buf->buf_ref != 0) {
        panic("buf_read: buffer still referenced");
    }

    // 设置新的block_num和引用计数
    buf->block_num = block_num;
    buf->buf_ref = 1;
    // 移动到已分配列表
    insert_head(buf_node, true);

    spinlock_release(&lk_buf_cache);
    spinlock_acquire(&buf->slk);
    // 3. 从磁盘读取数据
    virtio_disk_rw(buf, false); // false = read
    return buf;
}

// 写函数 (强制磁盘和内存保持一致)
void buf_write(buf_t* buf)
{
    if(!spinlock_holding(&buf->slk)) {
        panic("buf_write: buffer not locked");
    }

    virtio_disk_rw(buf, true); // true = write
}

// buf 释放
void buf_release(buf_t* buf)
{
    if(!spinlock_holding(&buf->slk)) {
        panic("buf_release: buffer not locked");
    }

    spinlock_release(&buf->slk);

    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref--;

    if(buf->buf_ref == 0) {
        // 找到对应的buf_node并移动到可分配列表
        buf_node_t* buf_node = (buf_node_t*)((char*)buf -
            ((char*)&((buf_node_t*)0)->buf - (char*)0));

        buf->block_num = BLOCK_NUM_UNUSED;
        insert_head(buf_node, false); // 移动到可分配列表
    }

    spinlock_release(&lk_buf_cache);
}

// 输出buf_cache的情况
void buf_print()
{
    printf("\nbuf_cache:\n");
    buf_node_t* buf = head_buf.next;
    spinlock_acquire(&lk_buf_cache);
    while(buf != &head_buf)
    {
        buf_t* b = &buf->buf;
        // 只显示 block_num 不为 BLOCK_NUM_UNUSED (-1) 的缓冲区
        if(b->block_num != BLOCK_NUM_UNUSED) {
            printf("buf %d: ref = %d, block_num = %d\n", (int)(buf-buf_cache), b->buf_ref, b->block_num);
            for(int i = 0; i < 8; i++)
                printf("%d ",b->data[i]);
            printf("\n");
        }
        buf = buf->next;
    }
    spinlock_release(&lk_buf_cache);
}

// 根据索引获取buf指针
buf_t* index_to_buf(int index)
{
    if(index < 0 || index >= N_BLOCK_BUF) {
        return NULL;
    }
    return &buf_cache[index].buf;
}

// 根据buf指针获取索引
int buf_to_index(buf_t* buf)
{
    buf_node_t* buf_node = (buf_node_t*)((char*)buf -
        ((char*)&((buf_node_t*)0)->buf - (char*)0));
    int index = buf_node - buf_cache;
    if(index < 0 || index >= N_BLOCK_BUF) {
        return -1;
    }
    return index;
}