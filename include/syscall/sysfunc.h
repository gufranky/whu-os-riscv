#ifndef __SYSFUNC_H__
#define __SYSFUNC_H__

#include "common.h"

uint64 sys_brk();
uint64 sys_mmap();
uint64 sys_munmap();
uint64 sys_copyin();
uint64 sys_copyout();
uint64 sys_copyinstr();

#endif