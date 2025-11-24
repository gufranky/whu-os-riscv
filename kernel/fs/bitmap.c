#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"

extern super_block_t sb;

// 搜索并设置bitmap中的第一个空闲bit
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    buf_t* buf = buf_read(bitmap_block);

    for(uint32 byte = 0; byte < BLOCK_SIZE; byte++) {
        if(buf->data[byte] != 0xFF) { // 如果这个字节不是全1，说明有空闲位
            for(uint8 bit = 0; bit < 8; bit++) {
                uint8 mask = 1 << bit;
                if(!(buf->data[byte] & mask)) { // 找到空闲bit
                    buf->data[byte] |= mask; // 设置bit为1
                    buf_write(buf);
                    buf_release(buf);
                    uint32 bit_num = byte * 8 + bit;
                    // bit 0 通常保留，从1开始分配
                    if(bit_num == 0) {
                        continue;
                    }
                    return bit_num; // 返回bit序号
                }
            }
        }
    }

    buf_release(buf);
    return 0; // 没有找到空闲bit
}

// 清除bitmap中指定的bit
static void bitmap_unset(uint32 bitmap_block, uint32 bit_num)
{
    if(bit_num == 0) {
        return; // bit 0 通常保留，不允许释放
    }

    buf_t* buf = buf_read(bitmap_block);

    uint32 byte = bit_num / 8;
    uint8 bit = bit_num % 8;

    // 边界检查
    if(byte >= BLOCK_SIZE) {
        buf_release(buf);
        return;
    }

    uint8 mask = 1 << bit;
    buf->data[byte] &= ~mask; // 清除bit
    buf_write(buf);
    buf_release(buf);
}

uint32 bitmap_alloc_block()
{
    // 从数据bitmap中分配一个数据块
    uint32 bit_num = bitmap_search_and_set(sb.data_bitmap_start);
    if(bit_num == 0) {
        return 0; // 分配失败
    }
    return sb.data_start + bit_num; // 返回实际的块号
}

void bitmap_free_block(uint32 block_num)
{
    // 释放数据块到数据bitmap
    if(block_num < sb.data_start) {
        return; // 无效的块号
    }
    uint32 bit_num = block_num - sb.data_start;
    bitmap_unset(sb.data_bitmap_start, bit_num);
}

uint16 bitmap_alloc_inode()
{
    // 从inode bitmap中分配一个inode
    uint32 bit_num = bitmap_search_and_set(sb.inode_bitmap_start);
    if(bit_num == 0) {
        return 0; // 分配失败
    }
    return (uint16)bit_num; // 返回inode号
}

void bitmap_free_inode(uint16 inode_num)
{
    // 释放inode到inode bitmap
    bitmap_unset(sb.inode_bitmap_start, inode_num);
}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(bitmap_block_num);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if(bit_cmp & buf->data[byte])
               printf("bit %d is alloced\n", byte * 8 + shift);
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("over\n");
    buf_release(buf);
}