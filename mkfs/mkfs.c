#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

// disk layout: [ super block | inode bitmap | inode blocks | data bitmap | data blocks ]

#define FS_MAGIC 0x12345678

// super block
typedef struct super_block {
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

// inode 64 byte
typedef struct inode_disk {
    short type;
    short major;
    short minor;
    short nlink;
    unsigned int size;
    unsigned int addrs[13];
} inode_disk_t;

// directory entry 32 byte
typedef struct dirent {
    unsigned short inode_num;
    char name[30];
} dirent_t;

// 文件类型
#define FT_UNUSED 0
#define FT_DIR    1
#define FT_FILE   2
#define FT_DEVICE 3 

// 常量定义 
#define BLOCK_SIZE       1024 // 每个block占1024字节
#define N_DATA_BLOCK     8192 // 1个block的bitmap管理的极限
#define N_INODE_BLOCK    128  // 支持2048个文件
#define N_BLOCK          (N_DATA_BLOCK + N_INODE_BLOCK + 3)  // 五个部分组合起来
#define INODE_PER_BLOCK  (BLOCK_SIZE / sizeof(inode_disk_t)) // 每个block里的inode数量
#define N_INODE          (N_INODE_BLOCK * INODE_PER_BLOCK)   // inode总数

// 与inode管理的data block相关
#define ENTRY_PER_BLOCK (BLOCK_SIZE / sizeof(unsigned int))  
#define N_ADDRS_1 10
#define N_ADDRS_2 2
#define N_ADDRS_3 1
#define N_ADDRS (N_ADDRS_1 + N_ADDRS_2 + N_ADDRS_3)

// 确定inode所在的inode block序号
#define INODE_LOCATE_BLOCK(inum, sb)  ((inum) / INODE_PER_BLOCK + sb.inode_start)

int fsfd;
super_block_t sb;

// 大小端转换
unsigned short xshort(unsigned short x)
{
    unsigned short y;
    unsigned char* a = (unsigned char*)&y;
    a[0] = x;
    a[1] = x >> 8;
    return y;
}

// 大小端转换
unsigned int xint(unsigned int x)
{
    unsigned int y;
    unsigned char* a = (unsigned char*)&y;
    a[0] = x;
    a[1] = x >> 8;
    a[2] = x >> 16;
    a[3] = x >> 24;
    return y;
}

// 向磁盘写一个block
void block_write(unsigned int block_num, void* buf)
{
    if(lseek(fsfd, BLOCK_SIZE * block_num, 0) != BLOCK_SIZE * block_num) {
        perror("lsek");
        exit(1);
    }
    if(write(fsfd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
        perror("write");
        exit(1);
    }
}

// 从磁盘读一个block
void block_read(unsigned int block_num, void* buf)
{
    if(lseek(fsfd, BLOCK_SIZE * block_num, 0) != BLOCK_SIZE * block_num) {
        perror("lsek");
        exit(1);
    }
    if(read(fsfd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
        perror("read");
        exit(1);
    }
}

// 申请一个block(修改bitmap)
unsigned int block_alloc()
{
    char buf[BLOCK_SIZE];
    unsigned int byte, shift;
    unsigned char bit_cmp;

    block_read(sb.data_bitmap_start, buf);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if((bit_cmp & buf[byte]) == 0) {
                buf[byte] |= bit_cmp;
                goto find;
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("block_alloc: no bit left\n");
    while(1);
find:
    block_write(sb.data_bitmap_start, buf);
    return byte * 8 + shift + sb.data_start;
}

// 申请一个inode (修改bitmap)
unsigned short inode_alloc()
{
    char buf[BLOCK_SIZE];
    unsigned int byte, shift;
    unsigned char bit_cmp;

    block_read(sb.inode_bitmap_start, buf);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if((bit_cmp & buf[byte]) == 0) {
                buf[byte] |= bit_cmp;
                goto find;
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("inode_alloc: no bit left\n");
    while(1);
find:
    block_write(sb.inode_bitmap_start, buf);
    return (unsigned short)(byte * 8 + shift);
}

// 从磁盘读一个inode
void inode_read(unsigned int inode_num, inode_disk_t* ip)
{
    char buf[BLOCK_SIZE];
    inode_disk_t* dip;

    unsigned int block_num = INODE_LOCATE_BLOCK(inode_num, sb);
    block_read(block_num, buf);
    dip = ((inode_disk_t*)buf) + (inode_num % INODE_PER_BLOCK);
    *ip = *dip;
}

// 向磁盘写一个inode
void inode_write(unsigned short inode_num, inode_disk_t* ip)
{
    char buf[BLOCK_SIZE];
    inode_disk_t* dip;

    unsigned int block_num = INODE_LOCATE_BLOCK(inode_num, sb);
    block_read(block_num, buf);
    dip = ((inode_disk_t*)buf) + (inode_num % INODE_PER_BLOCK);
    *dip = *ip;
    block_write(block_num, buf);
}

// 赋值并写一个inode
void inode_create(inode_disk_t* inode, unsigned short inode_num, unsigned short type)
{
    inode->type = xshort(type);
    inode->major = xshort(0);
    inode->minor = xshort(0);
    inode->nlink = xshort(1);
    inode->size = xint(0);
    for(int i = 0; i < N_ADDRS; i++)
        inode->addrs[i] = xint(0);
    inode_write(inode_num, inode);
}

// dirent_create 专用
char dir_buf[BLOCK_SIZE];

// 添加一个目录项
unsigned int dirent_create(unsigned int dir_block, unsigned int offset, char* name, unsigned short inode_num)
{
    dirent_t de;
    de.inode_num = xint(inode_num);
    assert(strlen(name) < 30);
    strcpy(de.name, name);

    block_read(dir_block, dir_buf);
    memmove(dir_buf + offset, &de, sizeof(de));
    block_write(dir_block, dir_buf);

    return offset + sizeof(dirent_t);
}

// 辅助 inode_locate_block
// 递归查询或创建block
static unsigned int locate_block(unsigned int* entry, unsigned int bn, unsigned int size)
{
    if(*entry == 0)
        *entry = block_alloc();

    if(size == 1)
        return *entry;    

    unsigned int* next_entry;
    unsigned int next_size = size / ENTRY_PER_BLOCK;
    unsigned int next_bn = bn % next_size;
    unsigned int ret = 0;

    char buf[BLOCK_SIZE];
    block_read(*entry, buf);
    next_entry = (unsigned int*)(buf) + bn / next_size;
    ret = locate_block(next_entry, next_bn, next_size);

    return ret;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
static unsigned int inode_locate_block(inode_disk_t* ip, unsigned int bn)
{
    // 在第一个区域
    if(bn < N_ADDRS_1)
        return locate_block(&ip->addrs[bn], bn, 1);

    // 在第二个区域
    bn -= N_ADDRS_1;
    if(bn < N_ADDRS_2 * ENTRY_PER_BLOCK)
    {
        unsigned int size = ENTRY_PER_BLOCK;
        unsigned int idx = bn / size;
        unsigned int b = bn % size;
        return locate_block(&ip->addrs[N_ADDRS_1 + idx], b, size);
    }

    // 在第三个区域
    bn -= N_ADDRS_2 * ENTRY_PER_BLOCK;
    if(bn < N_ADDRS_3 * ENTRY_PER_BLOCK * ENTRY_PER_BLOCK)
    {
        unsigned int size = ENTRY_PER_BLOCK * ENTRY_PER_BLOCK;
        unsigned int idx = bn / size;
        unsigned int b = bn % size;
        return locate_block(&ip->addrs[N_ADDRS_1 + N_ADDRS_2 + idx], b, size);
    }

    printf("inode_locate_block: overflow\n");
    while(1);

    return 0;
}

// main函数
int main(int argc, char* argv[])
{
    assert(BLOCK_SIZE % sizeof(inode_disk_t) == 0);
    
    // 创建磁盘文件
    fsfd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
    if(fsfd < 0) {
        perror(argv[1]);
        exit(1);
    }

    // super block 填充
    sb.magic = FS_MAGIC;
    sb.block_size = xint(BLOCK_SIZE);
    sb.inode_blocks = xint(N_INODE_BLOCK);
    sb.data_blocks = xint(N_DATA_BLOCK);
    sb.total_blocks = xint(N_BLOCK);
    sb.inode_bitmap_start = xint(1);
    sb.inode_start = xint(1 + 1);
    sb.data_bitmap_start = xint(1 + 1 + N_INODE_BLOCK);
    sb.data_start = xint(1 + 1 + N_INODE_BLOCK + 1);

    // 缓冲区准备
    char buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    // 一个全0的磁盘映像
    for(int i = 0; i < N_BLOCK; i++)
        block_write(i, buf);

    // 填写 super block
    memmove(buf, &sb, sizeof(sb));
    block_write(0, buf);

    // 创建根目录
    inode_disk_t rooti;
    unsigned short root_inum = inode_alloc();
    unsigned int rooti_block = block_alloc();
    if(root_inum != 0) {
        printf("rooti = %d\n", root_inum);
        while(1);
    }
    inode_create(&rooti, root_inum, FT_DIR);

    // 添加 . 和 ..
    unsigned int offset = 0;
    offset = dirent_create(rooti_block, offset, ".\0", root_inum);
    offset = dirent_create(rooti_block, offset, "..\0", root_inum);

    // 写入user目录里的可执行文件
    // ./user/_xxx
    char* shortname;
    int fd, read_len;
    inode_disk_t inode;
    unsigned short inum;
    unsigned int bn = 0, block_num = 0;

    for(int i = 2; i < argc; i++)
    {
        // 确定shortname
        shortname = argv[i] + 7;
        assert(*shortname == '_');
        assert(index(shortname, '/') == 0);
        shortname++;

        // 申请新的inode + 创建目录项
        inum = inode_alloc();
        inode_create(&inode, inum, FT_FILE);
        offset = dirent_create(rooti_block, offset, shortname, inum);
        
        // 打开文件
        fd = open(argv[i], 0);
        if(fd < 0) {
            perror(argv[i]);
            exit(1);
        }
        
        // 获取文件内容并写入磁盘
        while(1) {
            read_len = read(fd, buf, BLOCK_SIZE);
            block_num = inode_locate_block(&inode, bn++);
            block_write(block_num, buf);
            inode.size += read_len;
            if(read_len < BLOCK_SIZE) break;
        }
        
        // 关闭文件
        close(fd);

        // 写回inode
        for(int j = 0; j < N_ADDRS; j++)
            inode.addrs[j] = xint(inode.addrs[j]);
        inode.size = xint(inode.size);
        inode_write(inum, &inode);
    }

    // 更新rooti
    rooti.addrs[0] = xint(rooti_block);
    rooti.size = xint(sizeof(dirent_t) * argc);
    inode_write(root_inum, &rooti);

    return 0;
}