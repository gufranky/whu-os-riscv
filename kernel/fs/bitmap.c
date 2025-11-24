#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"

extern super_block_t sb;

// search and set bit
/*static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    return 0;
}*/

// unset bit
/*static void bitmap_unset(uint32 bitmap_block, uint32 num)
{
    
}*/

uint32 bitmap_alloc_block()
{
    return 0;
}

void bitmap_free_block(uint32 block_num)
{

}

uint16 bitmap_alloc_inode()
{
    return 0;
}

void bitmap_free_inode(uint16 inode_num)
{

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