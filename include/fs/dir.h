#ifndef __DIR_H__
#define __DIR_H__

#include "common.h"

#define DIR_NAME_LEN 30  // 最大目录名长度
#define DIR_PATH_LEN 128 // 最大文件路径长度

typedef struct dirent {
    uint16 inode_num;
    char name[DIR_NAME_LEN];
} dirent_t;

typedef struct inode inode_t;

uint16 dir_search_entry(inode_t* pip, char* name);
uint32 dir_add_entry(inode_t* pip, uint16 inode_num, char* name);
uint16 dir_delete_entry(inode_t* pip, char* name);
uint32 dir_get_entries(inode_t* pip, uint32 len, void* dst, bool user); // NEW
uint32 dir_change(char* path); // NEW
void   dir_print(inode_t* pip);


inode_t* path_to_inode(char* path);
inode_t* path_to_pinode(char* path, char* name);
inode_t* path_create_inode(char* path, uint16 type, uint16 major, uint16 minor); // NEW
uint32   path_link(char* old_path, char* new_path); // NEW
uint32   path_unlink(char* path); // NEW

#endif