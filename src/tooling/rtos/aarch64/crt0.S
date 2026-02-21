    .section .text.boot
    .global _start
    .extern main

_start:
    ldr x0, =__stack_top
    mov sp, x0
    bl main
1:
    wfi
    b 1b
