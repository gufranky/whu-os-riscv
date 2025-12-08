#ifndef __SYSFUNC_H__
#define __SYSFUNC_H__

#include "common.h"


// 文件系统无关的系统调用
uint64 sys_print();
uint64 sys_exec();
uint64 sys_brk();
uint64 sys_mmap();
uint64 sys_munmap();
uint64 sys_fork();
uint64 sys_wait();
uint64 sys_exit();
uint64 sys_sleep();

// 文件系统相关的系统调用

uint64 sys_open();
uint64 sys_close();
uint64 sys_read();
uint64 sys_write();
uint64 sys_lseek();
uint64 sys_dup();
uint64 sys_fstat();
uint64 sys_getdir();
uint64 sys_mkdir();
uint64 sys_chdir();
uint64 sys_link();
uint64 sys_unlink();
uint64 sys_alloc_block();
uint64 sys_free_block();
uint64 sys_show_buf();
uint64 sys_read_block();
uint64 sys_write_block();
uint64 sys_release_block();

#endif
