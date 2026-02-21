    .section .text.boot
    .global _start
    .extern main

_start:
    la sp, __stack_top
    call main
1:
    wfi
    j 1b
