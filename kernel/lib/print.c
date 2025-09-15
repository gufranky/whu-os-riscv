// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";

void print_init(void)
{

}

// Print to the console. only understands %d, %x, %p, %s.
void printf(const char *fmt, ...)
{

}

void panic(const char *s)
{

}

void assert(bool condition, const char* warning)
{

}
