#ifndef __UART_H__
#define __UART_H__

#include "common.h"

void uart_init(void);
void uart_putc_sync(int c);
int  uart_getc_sync(void);
void uart_intr(void);

#endif