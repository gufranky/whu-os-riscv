#ifndef __FS_H__
#define __FS_H__

#include "common.h"

// 超级块
typedef struct super_block
{
    unsigned int magic;
    unsigned int block_size;

    unsigned int inode_bitmap_start;
    unsigned int inode_start;
    unsigned int data_bitmap_start;
    unsigned int data_start;

    unsigned int inode_blocks;
    unsigned int data_blocks;
    unsigned int total_blocks;

} super_block_t;

void fs_init();

#endif