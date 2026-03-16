ENTRY(_start)

SECTIONS
{
  . = 0x80000000;
  .text : { *(.text.boot) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(16);
  __stack_top = . + 0x10000;
}
