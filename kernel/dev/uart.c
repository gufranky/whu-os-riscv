// low-level driver routines for 16550a UART.

#include "memlayout.h"
#include "lib/lock.h"

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html

#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_TX_ENABLE (1<<0)
#define IER_RX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

// 读写寄存器的宏定义
#define Reg(reg)         ((volatile unsigned char *)(UART_BASE + reg))
#define ReadReg(reg)     (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

extern volatile int panicked; // from printf.c

// uart 初始化
void uart_init(void)
{
  // 关闭中断
  WriteReg(IER, 0x00);

  // 进入设置比特率的模式
  WriteReg(LCR, LCR_BAUD_LATCH);

  // 设置比特率的低位和高位，最终设置为38.4K
  WriteReg(0, 0x03);
  WriteReg(1, 0x00);

  // 设置传输字节长度为8bit,不校验
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 清零和使能FIFO模式
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 使能输出队列和接收队列的中断
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
}

// 单个字符输出
void uart_putc_sync(int c)
{
  push_off();

  while(panicked);

  // 等待TX队列进入idle状态
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0);
  
  // 输出
  WriteReg(THR, c);

  pop_off();
}

// 单个字符输入
// 失败返回-1
int uart_getc_sync(void)
{
  if(ReadReg(LSR) & 0x01){
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// 中断处理(键盘输入->屏幕输出)
void uart_intr(void)
{
  while(1)
  {
    int c = uart_getc_sync();
    if(c == -1) break;
    uart_putc_sync(c);
  }
}
