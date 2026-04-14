.text
.p2align 2

#ifndef CHENG_V3_LINUX_ENTRY_SYMBOL
#define CHENG_V3_LINUX_ENTRY_SYMBOL cheng_v3_program_argv_entry
#endif

.globl _start
.type _start, %function
_start:
  ldr x0, [sp]
  add x1, sp, #8
  bl CHENG_V3_LINUX_ENTRY_SYMBOL
  mov x8, #93
  svc #0
1:
  b 1b
.size _start, . - _start
