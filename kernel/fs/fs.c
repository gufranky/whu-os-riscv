#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/file.h"
#include "lib/str.h"
#include "lib/print.h"

// 超级块在内存的副本
super_block_t sb;

#define FS_MAGIC 0x12345678
#define SB_BLOCK_NUM 0

uint8 tmp[2*BLOCK_SIZE];
uint8 str[2*BLOCK_SIZE];

// 比较两个数组的内容是否相同
// 返回true表示相同，false表示不同
static bool blockcmp(uint8* arr1, uint8* arr2)
{
    int diff_count = 0;
    for (int i = 0; i < BLOCK_SIZE * 2; i++) {
        if (arr1[i] != arr2[i]) {
            if (diff_count < 10) { // 只显示前10个不同的值
                printf("Diff at [%d]: arr1=%d, arr2=%d\n", i, arr1[i], arr2[i]);
            }
            diff_count++;
        }
    }

    if (diff_count > 0) {
        printf("Total differences: %d out of %d\n", diff_count, BLOCK_SIZE * 2);
        return false;
    }
    return true;
}

// 输出super_block的信息
static void sb_print()
{
    printf("\nsuper block information:\n");
    printf("magic = %x\n", sb.magic);
    printf("block size = %d\n", sb.block_size);
    printf("inode blocks = %d\n", sb.inode_blocks);
    printf("data blocks = %d\n", sb.data_blocks);
    printf("total blocks = %d\n", sb.total_blocks);
    printf("inode bitmap start = %d\n", sb.inode_bitmap_start);
    printf("inode start = %d\n", sb.inode_start);
    printf("data bitmap start = %d\n", sb.data_bitmap_start);
    printf("data start = %d\n", sb.data_start);
}

// 文件系统初始化
void fs_init()
{
    buf_init();

    buf_t* buf;

    buf = buf_read(SB_BLOCK_NUM);
    memmove(&sb, buf->data, sizeof(sb));
    assert(sb.magic == FS_MAGIC, "fs_init: magic");
    assert(sb.block_size == BLOCK_SIZE, "fs_init: block size");
    buf_release(buf);
    sb_print();

    inode_init();
    file_init();
    uint32 ret = 0;

    for(int i = 0; i < BLOCK_SIZE * 2; i++)
        str[i] = i;

    // 创建新的inode
    inode_t* nip = inode_create(FT_FILE, 0, 0);
    inode_lock(nip);
    
    // 第一次查看
    inode_print(nip);

    // 第一次写入
    ret = inode_write_data(nip, 0, BLOCK_SIZE / 2, str, false);
    assert(ret == BLOCK_SIZE / 2, "inode_write_data: fail");

    // 第二次写入
    ret = inode_write_data(nip, BLOCK_SIZE / 2, BLOCK_SIZE + BLOCK_SIZE / 2, str + BLOCK_SIZE / 2, false);
    assert(ret == BLOCK_SIZE +  BLOCK_SIZE / 2, "inode_write_data: fail");

    // 一次读取
    ret = inode_read_data(nip, 0, BLOCK_SIZE * 2, tmp, false);
    assert(ret == BLOCK_SIZE * 2, "inode_read_data: fail");

    // 第二次查看
    inode_print(nip);
    
    inode_unlock_free(nip);

    // 测试
    if(blockcmp(tmp, str) == true)
        printf("success");
    else
        printf("fail");

    printf("inode finish--------");
     // 创建inode
    inode_t* ip = inode_alloc(INODE_ROOT);
    inode_t* ip_1 = inode_create(FT_DIR, 0, 0);
    inode_t* ip_2 = inode_create(FT_DIR, 0, 0);
    inode_t* ip_3 = inode_create(FT_FILE, 0, 0);

    // 上锁
    inode_lock(ip);
    inode_lock(ip_1);
    inode_lock(ip_2);
    inode_lock(ip_3);

    // 创建目录
    dir_add_entry(ip, ip_1->inode_num, "user");
    dir_add_entry(ip_1, ip_2->inode_num, "work");
    dir_add_entry(ip_2, ip_3->inode_num, "hello.txt");
    
    // 填写文件
    inode_write_data(ip_3, 0, 11, "hello world", false);

    // 解锁
    inode_unlock(ip_3);
    inode_unlock(ip_2);
    inode_unlock(ip_1);
    inode_unlock(ip);

    // 路径查找
    char* path = "/user/work/hello.txt";
    char name[DIR_NAME_LEN];
    inode_t* tmp_1 = path_to_pinode(path, name);
    inode_t* tmp_2 = path_to_inode(path);

    assert(tmp_1 != NULL, "tmp1 = NULL");
    assert(tmp_2 != NULL, "tmp2 = NULL");
    printf("\nname = %s\n", name);

    // 输出 tmp_1 的信息
    inode_lock(tmp_1);
    inode_print(tmp_1);
    inode_unlock_free(tmp_1);

    // 输出 tmp_2 的信息
    inode_lock(tmp_2);
    inode_print(tmp_2);
    char str[12];
    str[11] = 0;
    inode_read_data(tmp_2, 0, tmp_2->size, str, false);
    printf("read: %s\n", str);
    inode_unlock_free(tmp_2);

    printf("------------over---------------------");
    inode_init();

    // 获取根目录
    ip = inode_alloc(INODE_ROOT);    
    inode_lock(ip);

    // 第一次查看
    dir_print(ip);
    
    // add entry
    dir_add_entry(ip, 1, "a.txt");
    dir_add_entry(ip, 2, "b.txt");
    dir_add_entry(ip, 3, "c.txt");
    
    // 第二次查看
    dir_print(ip);

    // 第一次检查
    assert(dir_search_entry(ip, "b.txt") == 2, "error-1");

    // delete entry
    dir_delete_entry(ip, "a.txt");
   
    // 第三次查看
    dir_print(ip);
    
    // add entry
    dir_add_entry(ip, 1, "d.txt");    
    
    // 第四次查看
    dir_print(ip);
    
    // 第二次检查
    assert(dir_add_entry(ip, 4, "d.txt") == BLOCK_SIZE, "error-2");
    
    inode_unlock(ip);

    printf("over");
   // while(1);


}