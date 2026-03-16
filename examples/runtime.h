#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Undefine macros that might conflict with our explicit declarations
#undef memset
#undef memcpy
#undef memcmp
#undef strlen
#undef strcmp
#undef fopen
#undef fclose
#undef fread
#undef fwrite
#undef fflush
#undef fgetc

// Macros used by generated C code
#ifndef IL64
#define IL64(x) ((long long)(x))
#endif

// Helper functions declared in system.cheng
void* ptr_add(void* p, int offset);
void sys_memset(void* dest, int val, int n);

// Address-of macro
#define addr_0(x) (&(x))

// Arithmetic intrinsics
int mul_0(int a, int b);
int div_0(int a, int b);
int mod_0(int a, int b);
int shl_0(int a, int b);
int shr_0(int a, int b);
int bitand_0(int a, int b);
int bitor_0(int a, int b);
int xor_0(int a, int b);
int bitnot_0(int a);

// String/Mem intrinsics often macro-ed but if called directly:
// c_strlen, c_memcpy, c_memset, c_strcmp are mapped to libc in system.cheng imports?
// imports were: @importc("strlen") proc c_strlen...
// So they map to standard 'strlen'.

static inline int my_ungetc(int c, void* stream) {
    return ungetc(c, (FILE*)stream);
}

#endif
