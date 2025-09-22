# 2025/9/15 最小系统

## 系统设计部分

### 总体设计目标

本最小系统的设计目标是构建一个精简但结构完整的RISC-V操作系统基础框架，重点实现：

1. 基础引导：完成从机器模式到监督模式的切换
2. 串口通信：实现基本的输入输出功能
3. 同步机制：提供基本的同步原语（自旋锁）
4. 中断管理：支持基本的中断处理机制

### 系统特性

1. **简洁性**
   - 仅保留核心功能，去除多余组件
   - 代码结构清晰，易于理解和扩展
   - 聚焦于基础功能的正确实现

2. **可扩展性**
   - 分层设计便于后续功能扩展
   - 预留了多核支持的基础设施
   - 模块化的代码组织

3. **可靠性**
   - 实现基本的同步机制
   - 支持中断的正确处理
   - 提供稳定的串口通信

### 核心功能设计

1. **引导启动设计**
   - 使用`entry.S`完成初始栈设置
   - 通过`start.c`实现特权级切换
   - 在`main.c`中完成基础初始化

2. **设备管理设计**
   - UART设备驱动
     * 支持字符输入输出
     * 实现基本的中断处理
     * 提供格式化打印功能

3. **同步机制设计**
   - 自旋锁实现
     * 支持基本的互斥操作
     * 包含死锁检测功能
     * 提供中断保护机制

4. **内存管理设计**
   - 通过链接脚本(`kernel.ld`)定义内存布局
   - 合理规划各个段的位置和大小
   - 预留扩展空间

### 安全性考虑

1. **特权级控制**
   - 严格的特权级切换机制
   - 合理的PMP配置
   - 中断保护机制

2. **同步保护**
   - 完整的中断禁用/使能机制
   - 自旋锁的正确实现
   - 死锁检测支持

## 架构设计说明

本最小 RISC-V 操作系统采用了分层架构设计，简单完成了最小系统包装，主要包括以下几个层次，为后续发展提供了些许框架：

### 1. 硬件抽象层

- **UART 驱动**：提供串口通信功能，实现基本的字符输入输出
- **内存管理**：通过链接脚本定义内存布局，管理代码段、数据段、BSS段
- **特权级管理**：实现从 Machine 模式到 Supervisor 模式的切换

### 2. 内核启动层

- **引导程序**：`entry.S` 负责设置栈空间和跳转到 C 代码
- **初始化程序**：`start.c` 完成特权级切换和 PMP 配置
- **主程序**：`main.c` 执行内核主逻辑

### 3. 系统服务层

- **打印服务**：格式化输出功能，支持字符串和整数打印
- **基础类型定义**：提供标准的数据类型定义
- **自旋锁**:为多线程的print做好了准备

### 目录结构设计

```
WHU-OS
├── include
│   │   └── uart.h
│   ├── lib
│   │   ├── print.h
│   │   └── lock.h
│   ├── proc
│   │   └── cpu.h
│   ├── common.h
│   ├── memlayout.h
│   └── riscv.h
├── kernel
│   ├── boot
│   │   ├── main.c  (TODO)
│   │   ├── start.c (TODO)
│   │   ├── entry.S
│   │   └── Makefile
│   ├── dev
│   │   ├── uart.c
│   │   └── Makefile
│   ├── lib
│   │   ├── print.c (TODO)
│   │   ├── spinlock.c (TODO)
│   │   └── Makefile
│   ├── proc
│   │   ├── cpu.c  (TODO)
│   │   └── Makefile
│   ├── Makefile
│   └── kernel.ld
├── Makefile
└── common.mk
```

## 关键数据结构

### 1. CPU结构体（cpu_t）

```c
typedef struct cpu {
    int noff;       // 关中断的深度
    int origin;     // 第一次关中断前的状态
} cpu_t;
```

CPU结构体是系统中用于管理CPU中断状态的重要数据结构：

- `noff`：表示关中断的嵌套深度，每次调用push_off()时增加，调用pop_off()时减少
- `origin`：保存第一次关中断前的CPU中断状态，用于最后一次pop_off时恢复原始状态
- 这种设计支持中断的嵌套关闭，确保临界区的正确保护

### 2. 自旋锁结构体（spinlock_t）

```c
typedef struct spinlock {
    int locked;     // 是否已锁定
    char* name;     // 锁的名称，用于调试
    int cpuid;      // 持有锁的CPU的ID
} spinlock_t;
```

自旋锁是操作系统中保护共享资源的基础同步机制：

- `locked`：标志位，表示锁是否被获取
- `name`：锁的名称，便于调试和跟踪
- `cpuid`：记录当前持有锁的CPU ID，用于检测死锁和调试

设计特点：

1. 简洁性：结构体设计简单，仅包含必要信息
2. 可调试性：包含name字段方便调试
3. 安全性：通过cpuid字段可以检测潜在的死锁情况

这些数据结构为系统提供了基本的同步原语，支持了安全的并发操作和临界区保护。虽然本系统当前是单核设计，但这些结构的存在为后续扩展到多核系统提供了基础。

## 与 xv6 对比分析

### 相似性

1. 本系统学习了xv6的启动方式。启动流程一致

### 设计理念差异

- **本系统**：删除了目前不存在的初始化内容，将代码改为单核性的代码。

## 设计决策理由

## 实验过程部分

## 实现步骤记录

## 问题与解决方案

## 源码理解总结

### 启动流程详解

#### 1. 引导阶段 (entry.S)

```assembly
_entry:
    # 设置栈指针
    la sp, stack0
    li a0, 1024*4
    csrr a1, mhartid
    addi a1, a1, 1
    mul a0, a0, a1
    add sp, sp, a0
    # 跳转到 C 代码
    call start
```

- 给单CPU分配4KB空间
- 跳转到 `start()` 函数

#### 2. 初始化阶段 (start.c)

```c
void start() {
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x);
    w_mepc((uint64)main);
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);
    asm volatile("mret");
}
```

**关键操作：**

- **特权级切换**：从 Machine 模式切换到 Supervisor 模式
- **PMP 配置**：配置物理内存保护，允许全地址空间读写执行
- **异常返回**：使用 `mret` 指令跳转到 `main()` 函数

#### 3. 主程序阶段 (main.c)

```c
void main() {
    uartinit();
    print_uart("xv6 kernel is booting\n");
    print_uart("Kernel initialization completed successfully!\n");
    while(1);
}
```

### UART 驱动实现分析

#### 初始化序列

```c
void uartinit(void) {
    WriteReg(IER, 0x00);              // 1. 禁用所有中断
    WriteReg(LCR, LCR_BAUD_LATCH);    // 2. 进入波特率设置模式
    WriteReg(0, 0x03);                // 3. 设置波特率低字节
    WriteReg(1, 0x00);                // 4. 设置波特率高字节
    WriteReg(LCR, LCR_EIGHT_BITS);    // 5. 退出波特率模式，设置8位数据
    WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR); // 6. 启用并清空FIFO
    WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);    // 7. 启用收发中断
}
```

#### 同步发送实现

```c
void uartputc_sync(int c) {
    while((ReadReg(LSR) & LSR_TX_IDLE) == 0); // 轮询等待发送器空闲
    WriteReg(THR, c);                          // 写入字符到发送寄存器
}
```

### 关键技术点分析

### 1. 内存映射

```ld
SECTIONS {
    . = 0x80000000;          # QEMU 内核加载地址
    .text : { ... }          # 代码段
    .rodata : { ... }        # 只读数据段  
    .data : { ... }          # 数据段
    .bss : { ... }           # BSS段
}
```

#### 2. PMP 机制

物理内存保护配置：

- `pmpaddr0 = 0x3FFFFFFFFFFFFF`：设置保护范围
- `pmpcfg0 = 0xF`：允许读写执行权限

### 编译链接流程

#### Makefile 执行顺序

#### 依赖关系管理

## 测试验证部分

## 功能测试结果

## 性能数据

## 异常测试

## 运行截图/录屏
