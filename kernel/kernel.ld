OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    PROVIDE(sbss = .);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
    PROVIDE(ebss = .);
  }


  /* 内核数据段结束，KERNEL_DATA 指向此处 */
  PROVIDE(KERNEL_DATA = .);

  /* 可分配物理页起始，ALIGN到4KB页 */
  . = ALIGN(0x1000);
  PROVIDE(ALLOC_BEGIN = .);

  /* 假设物理内存128MB，可分配区域到0x88000000 */
  . = 0x88000000;
  PROVIDE(ALLOC_END = .);

  /* 内核结束符号 */
  PROVIDE(end = .);
}
