#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "proc/cpu.h"

// 对目录文件的简化性假设: 每个目录文件只包括一个block
// 也就是每个目录下最多 BLOCK_SIZE / sizeof(dirent_t) = 32 个目录项

// 查询一个目录项是否在目录里
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_search_entry(inode_t *pip, char *name)
{
    assert(spinlock_holding(&pip->slk), "dir_search_entry: lock");
    assert(pip->type == FT_DIR, "dir_search_entry: not dir");

    buf_t *buf = buf_read(pip->addrs[0]);
    dirent_t *de;

    for (uint32 offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] != 0 && strncmp(de->name, name, DIR_NAME_LEN) == 0)
        {
            uint16 inode_num = de->inode_num;
            buf_release(buf);
            return inode_num;
        }
    }

    buf_release(buf);
    return INODE_NUM_UNUSED;
}

// 在pip目录下添加一个目录项
// 成功返回这个目录项的偏移量 (同时更新pip->size)
// 失败返回BLOCK_SIZE (没有空间 或 发生重名)
// ps: 调用者需持有pip的锁
uint32 dir_add_entry(inode_t *pip, uint16 inode_num, char *name)
{
    assert(spinlock_holding(&pip->slk), "dir_add_entry: lock");
    assert(pip->type == FT_DIR, "dir_add_entry: not dir");

    if (strlen(name) >= DIR_NAME_LEN)
        return BLOCK_SIZE;

    if (dir_search_entry(pip, name) != INODE_NUM_UNUSED)
        return BLOCK_SIZE;

    buf_t *buf = buf_read(pip->addrs[0]);
    dirent_t *de;

    for (uint32 offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] == 0)
        {
            de->inode_num = inode_num;
            strncpy(de->name, name, DIR_NAME_LEN);
            de->name[DIR_NAME_LEN - 1] = 0;
            buf_write(buf);
            buf_release(buf);

            pip->size = offset + sizeof(dirent_t);
            return offset;
        }
    }

    buf_release(buf);
    return BLOCK_SIZE;
}

// 在pip目录下删除一个目录项
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_delete_entry(inode_t *pip, char *name)
{
    assert(spinlock_holding(&pip->slk), "dir_delete_entry: lock");
    assert(pip->type == FT_DIR, "dir_delete_entry: not dir");

    buf_t *buf = buf_read(pip->addrs[0]);
    dirent_t *de;

    for (uint32 offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] != 0 && strncmp(de->name, name, DIR_NAME_LEN) == 0)
        {
            uint16 inode_num = de->inode_num;
            de->name[0] = 0;
            de->inode_num = INODE_NUM_UNUSED;
            buf_write(buf);
            buf_release(buf);
            return inode_num;
        }
    }

    buf_release(buf);
    return INODE_NUM_UNUSED;
}

// 把目录下的有效目录项复制到dst (dst区域长度为len)
// 返回读到的字节数 (sizeof(dirent_t)*n)
// 调用者需要持有pip的锁
uint32 dir_get_entries(inode_t* pip, uint32 len, void* dst, bool user)
{
    assert(spinlock_holding(&pip->slk), "dir_get_entries: lock");
    assert(pip->type == FT_DIR, "dir_get_entries: not dir");

    buf_t *buf = buf_read(pip->addrs[0]);
    dirent_t *de;
    uint32 copied = 0;
    char *dst_ptr = (char *)dst;

    for (uint32 offset = 0; offset < BLOCK_SIZE && copied + sizeof(dirent_t) <= len; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] != 0)
        {
            if (user) {
                // 如果是用户空间，需要使用特殊的拷贝函数 (这里暂时直接拷贝)
                memmove(dst_ptr + copied, de, sizeof(dirent_t));
            } else {
                memmove(dst_ptr + copied, de, sizeof(dirent_t));
            }
            copied += sizeof(dirent_t);
        }
    }

    buf_release(buf);
    return copied;
}

// 改变进程里存储的当前目录
// 成功返回0 失败返回-1
uint32 dir_change(char* path)
{
    // 这是一个简化实现，实际系统中需要维护进程的当前工作目录
    // 这里我们只验证路径是否有效
    inode_t* ip = path_to_inode(path);
    if (ip == NULL)
        return -1;

    inode_lock(ip);
    if (ip->type != FT_DIR) {
        inode_unlock_free(ip);
        return -1;
    }
    inode_unlock_free(ip);

    // 在实际实现中，这里应该设置进程的cwd字段
    // 由于没有找到进程结构中的cwd字段，这里只做路径验证
    return 0;
}

// 输出一个目录下的所有有效目录项
// for debug
// ps: 调用者需持有pip的锁
void dir_print(inode_t *pip)
{
    assert(spinlock_holding(&pip->slk), "dir_print: lock");

    printf("\ninode_num = %d dirents:\n", pip->inode_num);

    dirent_t *de;
    buf_t *buf = buf_read(pip->addrs[0]);
    for (uint32 offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t))
    {
        de = (dirent_t *)(buf->data + offset);
        if (de->name[0] != 0)
            printf("inum = %d dirent = %s\n", de->inode_num, de->name);
    }
    buf_release(buf);
}

/*----------------------- 路径(一串目录和文件) -------------------------*/

// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
static char *skip_element(char *path, char *name)
{
    while(*path == '/') path++;
    if(*path == 0) return 0;

    char *s = path;
    while (*path != '/' && *path != 0)
        path++;

    int len = path - s;
    if (len >= DIR_NAME_LEN) {
        memmove(name, s, DIR_NAME_LEN);
        name[DIR_NAME_LEN - 1] = 0;
    } else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}
// 查找路径path对应的inode (find_parent = false)
// 查找路径path对应的inode的父节点 (find_parent = true)
// 供两个上层函数使用
// 失败返回NULL
static inode_t* search_inode(char* path, char* name, bool find_parent)
{
    inode_t *ip;

    // 确定起始目录：根目录还是当前目录
    if (path[0] == '/') {
        ip = inode_alloc(INODE_ROOT);
    } else {
        // 简化实现：假设当前目录就是根目录
        // 实际实现中应该获取进程的当前工作目录
        ip = inode_alloc(INODE_ROOT);
    }

    if (ip == NULL)
        return NULL;

    while ((path = skip_element(path, name)) != 0) {
        inode_lock(ip);
        if (ip->type != FT_DIR) {
            inode_unlock_free(ip);
            return NULL;
        }

        if (find_parent && *path == '\0') {
            // 找到父目录，返回当前inode
            inode_unlock(ip);
            return ip;
        }

        uint16 next_inum = dir_search_entry(ip, name);
        if (next_inum == INODE_NUM_UNUSED) {
            inode_unlock_free(ip);
            return NULL;
        }
        inode_unlock(ip);
        inode_free(ip);

        ip = inode_alloc(next_inum);
        if (ip == NULL)
            return NULL;
    }

    if (find_parent) {
        inode_free(ip);
        return NULL;
    }

    return ip;
}

// 找到path对应的inode
inode_t* path_to_inode(char* path)
{
    char name[DIR_NAME_LEN];
    return search_inode(path, name, false);
}

// 找到path对应的inode的父节点
// path最后的目录名放入name指向的空间
inode_t* path_to_pinode(char* path, char* name)
{
    return search_inode(path, name, true);
}

// 如果path对应的inode存在则返回inode
// 如果path对应的inode不存在则创建inode
// 失败返回NULL
inode_t* path_create_inode(char* path, uint16 type, uint16 major, uint16 minor)
{
    char name[DIR_NAME_LEN];
    inode_t *dir_ip = path_to_pinode(path, name);
    if (dir_ip == NULL)
        return NULL;

    inode_lock(dir_ip);

    // 检查是否已经存在
    if (dir_search_entry(dir_ip, name) != INODE_NUM_UNUSED) {
        inode_unlock_free(dir_ip);
        return NULL;
    }

    // 创建新的inode
    inode_t *ip = inode_create(type, major, minor);
    if (ip == NULL) {
        inode_unlock_free(dir_ip);
        return NULL;
    }

    inode_lock(ip);

    // 在父目录中添加目录项
    uint32 offset = dir_add_entry(dir_ip, ip->inode_num, name);
    if (offset == BLOCK_SIZE) {
        // 添加失败，清理资源
        inode_unlock(ip);
        inode_free_data(ip);
        inode_free(ip);
        inode_unlock_free(dir_ip);
        return NULL;
    }

    // 如果是目录，添加 "." 和 ".." 目录项
    if (type == FT_DIR) {
        // 分配一个数据块
        uint32 block_num = bitmap_alloc_block();
        if (block_num == 0) {
            inode_unlock(ip);
            inode_free_data(ip);
            inode_free(ip);
            dir_delete_entry(dir_ip, name);
            inode_unlock_free(dir_ip);
            return NULL;
        }
        ip->addrs[0] = block_num;
        ip->size = 0;

        // 添加 "." 目录项
        dir_add_entry(ip, ip->inode_num, ".");
        // 添加 ".." 目录项
        dir_add_entry(ip, dir_ip->inode_num, "..");

        // 增加父目录的链接数
        dir_ip->nlink++;
        inode_rw(dir_ip, true);
    }

    // 增加新inode的链接数
    ip->nlink = 1;
    inode_rw(ip, true);

    inode_unlock(ip);
    inode_unlock_free(dir_ip);
    return ip;
}

// 文件链接(目录不能被链接)
// 本质是创建一个目录项, 这个目录项的inode_num是存在的而不用申请
// 成功返回0 失败返回-1
uint32 path_link(char* old_path, char* new_path)
{
    char new_name[DIR_NAME_LEN];
    inode_t *old_ip, *dir_ip;

    // 获取旧文件的inode
    old_ip = path_to_inode(old_path);
    if (old_ip == NULL)
        return -1;

    inode_lock(old_ip);

    // 目录不能链接
    if (old_ip->type == FT_DIR) {
        inode_unlock_free(old_ip);
        return -1;
    }

    // 获取新路径的父目录
    dir_ip = path_to_pinode(new_path, new_name);
    if (dir_ip == NULL) {
        inode_unlock_free(old_ip);
        return -1;
    }

    inode_lock(dir_ip);

    // 在新目录中添加目录项
    uint32 offset = dir_add_entry(dir_ip, old_ip->inode_num, new_name);
    if (offset == BLOCK_SIZE) {
        inode_unlock_free(dir_ip);
        inode_unlock_free(old_ip);
        return -1;
    }

    // 增加链接数
    old_ip->nlink++;
    inode_rw(old_ip, true);

    inode_unlock_free(dir_ip);
    inode_unlock_free(old_ip);
    return 0;
}

// 检查一个unlink操作是否合理
// 调用者需要持有ip的锁
// 在path_unlink()中调用
static bool check_unlink(inode_t* ip)
{
    assert(spinlock_holding(&ip->slk), "check_unlink: slk");

    uint8 tmp[sizeof(dirent_t) * 3];
    uint32 read_len;

    read_len = dir_get_entries(ip, sizeof(dirent_t) * 3, tmp, false);

    if(read_len == sizeof(dirent_t) * 3) {
        return false;
    } else if(read_len == sizeof(dirent_t) * 2) {
        return true;
    } else {
        panic("check_unlink: read_len");
        return false;
    }
}
// 文件删除链接
uint32 path_unlink(char* path)
{
    char name[DIR_NAME_LEN];
    inode_t *dir_ip, *ip;

    // 获取父目录
    dir_ip = path_to_pinode(path, name);
    if (dir_ip == NULL)
        return -1;

    inode_lock(dir_ip);

    // 不能删除 "." 和 ".."
    if (strncmp(name, ".", DIR_NAME_LEN) == 0 || strncmp(name, "..", DIR_NAME_LEN) == 0) {
        inode_unlock_free(dir_ip);
        return -1;
    }

    // 查找要删除的文件
    uint16 inum = dir_search_entry(dir_ip, name);
    if (inum == INODE_NUM_UNUSED) {
        inode_unlock_free(dir_ip);
        return -1;
    }

    ip = inode_alloc(inum);
    if (ip == NULL) {
        inode_unlock_free(dir_ip);
        return -1;
    }

    inode_lock(ip);

    // 如果是目录，检查是否为空（只包含.和..）
    if (ip->type == FT_DIR) {
        if (!check_unlink(ip)) {
            inode_unlock_free(ip);
            inode_unlock_free(dir_ip);
            return -1;
        }

        // 减少父目录的链接数
        dir_ip->nlink--;
        inode_rw(dir_ip, true);
    }

    // 从父目录中删除目录项
    dir_delete_entry(dir_ip, name);
    inode_unlock_free(dir_ip);

    // 减少文件的链接数
    ip->nlink--;
    if (ip->nlink == 0) {
        // 如果链接数为0，释放文件数据
        inode_free_data(ip);
    }
    inode_rw(ip, true);
    inode_unlock_free(ip);

    return 0;
}