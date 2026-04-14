.text
.p2align 2

.macro CHENG_V3_LINUX_SYSCALL name, argc
.globl \name
.type \name, %function
\name:
  mov x8, x0
.if \argc >= 1
  mov x0, x1
.endif
.if \argc >= 2
  mov x1, x2
.endif
.if \argc >= 3
  mov x2, x3
.endif
.if \argc >= 4
  mov x3, x4
.endif
.if \argc >= 5
  mov x4, x5
.endif
.if \argc >= 6
  mov x5, x6
.endif
  svc #0
  ret
.size \name, . - \name
.endm

CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall0, 0
CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall1, 1
CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall2, 2
CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall3, 3
CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall4, 4
CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall5, 5
CHENG_V3_LINUX_SYSCALL __cheng_linux_syscall6, 6

#ifndef CHENG_V3_LINUX_ENTRY_SYMBOL
#define CHENG_V3_LINUX_ENTRY_SYMBOL cheng_v3_program_argv_entry
#endif

.globl _start
.type _start, %function
_start:
  ldr x0, [sp]
  add x1, sp, #8
  mov x19, x0
  mov x20, x1
  bl __cheng_setCmdLine
  mov x0, x19
  mov x1, x20
  bl CHENG_V3_LINUX_ENTRY_SYMBOL
  mov x8, #93
  svc #0
1:
  b 1b
.size _start, . - _start
