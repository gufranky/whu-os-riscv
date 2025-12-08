#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/file.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "lib/print.h"
#include "lib/str.h"

// 设备列表(读写接口)
dev_t devlist[N_DEV];

// ftable + 保护它的锁
#define N_FILE 32
file_t ftable[N_FILE];
spinlock_t lk_ftable;

// ftable初始化 + devlist初始化
void file_init()
{
    spinlock_init(&lk_ftable, "ftable");

    // 初始化文件表
    for (int i = 0; i < N_FILE; i++) {
        ftable[i].type = FD_UNUSED;
        ftable[i].ref = 0;
        ftable[i].readable = false;
        ftable[i].writable = false;
        ftable[i].ip = NULL;
        ftable[i].offset = 0;
    }

    // 初始化设备表
    for (int i = 0; i < N_DEV; i++) {
        devlist[i].read = NULL;
        devlist[i].write = NULL;
    }
}

// alloc file_t in ftable
// 失败则panic
file_t* file_alloc()
{
    spinlock_acquire(&lk_ftable);

    for (int i = 0; i < N_FILE; i++) {
        if (ftable[i].type == FD_UNUSED) {
            ftable[i].type = FD_FILE; // 默认类型，稍后会被覆盖
            ftable[i].ref = 1;
            ftable[i].readable = false;
            ftable[i].writable = false;
            ftable[i].ip = NULL;
            ftable[i].offset = 0;
            spinlock_release(&lk_ftable);
            return &ftable[i];
        }
    }

    spinlock_release(&lk_ftable);
    panic("file_alloc: no free file entries");
    return NULL;
}

// 创建设备文件(供proczero创建console)
file_t* file_create_dev(char* path, uint16 major, uint16 minor)
{
    inode_t* ip = path_create_inode(path, FT_DEVICE, major, minor);
    if (ip == NULL)
        return NULL;

    file_t* file = file_alloc();
    file->type = FD_DEVICE;
    file->readable = true;
    file->writable = true;
    file->major = major;
    file->ip = ip;

    return file;
}

// 打开一个文件
file_t* file_open(char* path, uint32 open_mode)
{
    inode_t* ip;

    // 如果需要创建文件但文件不存在
    if (open_mode & MODE_CREATE) {
        ip = path_create_inode(path, FT_FILE, 0, 0);
        if (ip == NULL)
            return NULL;
    } else {
        ip = path_to_inode(path);
        if (ip == NULL)
            return NULL;
    }

    inode_lock(ip);

    // 检查文件类型和权限
    if (ip->type == FT_DIR && (open_mode & MODE_WRITE)) {
        inode_unlock_free(ip);
        return NULL;
    }

    file_t* file = file_alloc();
    if (file == NULL) {
        inode_unlock_free(ip);
        return NULL;
    }

    if (ip->type == FT_DIR)
        file->type = FD_DIR;
    else if (ip->type == FT_FILE)
        file->type = FD_FILE;
    else if (ip->type == FT_DEVICE) {
        file->type = FD_DEVICE;
        file->major = ip->major;
    }

    file->readable = (open_mode & MODE_READ) ? true : false;
    file->writable = (open_mode & MODE_WRITE) ? true : false;
    file->ip = ip;
    file->offset = 0;

    inode_unlock(ip);
    return file;
}

// 释放一个file
void file_close(file_t* file)
{
    spinlock_acquire(&lk_ftable);

    if (file->ref < 1)
        panic("file_close: ref count");

    file->ref--;
    if (file->ref == 0) {
        file_t temp = *file; // 备份，释放锁后使用
        file->type = FD_UNUSED;
        file->ref = 0;
        file->readable = false;
        file->writable = false;
        file->ip = NULL;
        file->offset = 0;

        spinlock_release(&lk_ftable);

        if (temp.ip) {
            inode_free(temp.ip);
        }
    } else {
        spinlock_release(&lk_ftable);
    }
}

// 文件内容读取
// 返回读取到的字节数
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool user)
{
    if (!file->readable)
        return 0;

    switch (file->type) {
    case FD_FILE:
    case FD_DIR:
        inode_lock(file->ip);
        if (file->type == FD_DIR) {
            uint32 read_len = dir_get_entries(file->ip, len, (void*)dst, user);
            inode_unlock(file->ip);
            return read_len;
        } else {
            uint32 read_len = inode_read_data(file->ip, file->offset, len, (void*)dst, user);
            file->offset += read_len;
            inode_unlock(file->ip);
            return read_len;
        }
    case FD_DEVICE:
        if (file->major >= N_DEV || devlist[file->major].read == NULL)
            return 0;
        return devlist[file->major].read(len, dst, user);
    default:
        return 0;
    }
}

// 文件内容写入
// 返回写入的字节数
uint32 file_write(file_t* file, uint32 len, uint64 src, bool user)
{
    if (!file->writable)
        return 0;

    switch (file->type) {
    case FD_FILE:
        inode_lock(file->ip);
        uint32 written = inode_write_data(file->ip, file->offset, len, (void*)src, user);
        file->offset += written;
        inode_unlock(file->ip);
        return written;
    case FD_DEVICE:
        if (file->major >= N_DEV || devlist[file->major].write == NULL)
            return 0;
        return devlist[file->major].write(len, src, user);
    case FD_DIR:
        return 0; // 目录不能写入
    default:
        return 0;
    }
}

// flags 可能取值
#define LSEEK_SET 0  // file->offset = offset
#define LSEEK_ADD 1  // file->offset += offset
#define LSEEK_SUB 2  // file->offset -= offset

// 修改file->offset (只针对FD_FILE类型的文件)
uint32 file_lseek(file_t* file, uint32 offset, int flags)
{
    if (file->type != FD_FILE)
        return -1;

    switch (flags) {
    case LSEEK_SET:
        file->offset = offset;
        break;
    case LSEEK_ADD:
        file->offset += offset;
        break;
    case LSEEK_SUB:
        if (file->offset >= offset)
            file->offset -= offset;
        else
            file->offset = 0;
        break;
    default:
        return -1;
    }

    return file->offset;
}

// file->ref++ with lock
file_t* file_dup(file_t* file)
{
    spinlock_acquire(&lk_ftable);
    assert(file->ref > 0, "file_dup: ref");
    file->ref++;
    spinlock_release(&lk_ftable);
    return file;
}

// 获取文件状态
int file_stat(file_t* file, uint64 addr)
{
    file_state_t state;
    if(file->type == FD_FILE || file->type == FD_DIR)
    {
        inode_lock(file->ip);
        state.type = file->ip->type;
        state.inode_num = file->ip->inode_num;
        state.nlink = file->ip->nlink;
        state.size = file->ip->size;
        inode_unlock(file->ip);

        uvm_copyout(myproc()->pgtbl, addr, (uint64)&state, sizeof(file_state_t));
        return 0;
    }
    return -1;
}