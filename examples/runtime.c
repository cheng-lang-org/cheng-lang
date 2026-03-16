#include "runtime.h"

void* ptr_add(void* p, int offset) {
    return (char*)p + offset;
}

void sys_memset(void* dest, int val, int n) {
    memset(dest, val, n);
}

int mul_0(int a, int b) { return a * b; }
int div_0(int a, int b) { return a / b; }
int mod_0(int a, int b) { return a % b; }
int shl_0(int a, int b) { return a << b; }
int shr_0(int a, int b) { return a >> b; }
int bitand_0(int a, int b) { return a & b; }
int bitor_0(int a, int b) { return a | b; }
int xor_0(int a, int b) { return a ^ b; }
int bitnot_0(int a) { return ~a; }

// Wrappers for conflicting libc functions
// We use long long for Cheng int (64-bit), int for int32.
// pointer is void*. cstring is char*.

int puts_0(char* s) {
    return puts(s);
}

void* malloc_0(int size) {
    return malloc((size_t)size);
}

void free_0(void* p) {
    free(p);
}

void* realloc_0(void* p, int size) {
    return realloc(p, (size_t)size);
}

void* c_memset_0(void* dest, int val, long long n) {
    return memset(dest, val, (size_t)n);
}

void* c_memcpy_0(void* dest, void* src, long long n) {
    return memcpy(dest, src, (size_t)n);
}

int c_strlen_0(char* s) {
    return (int)strlen(s);
}

int c_strcmp_0(char* a, char* b) {
    return strcmp(a, b);
}

int cmpMem_0(void* a, void* b, long long n) {
    return memcmp(a, b, (size_t)n);
}

void* c_fopen_0(char* filename, char* mode) {
    return fopen(filename, mode);
}

int c_fclose_0(void* f) {
    return fclose((FILE*)f);
}

int c_fread_0(void* ptr, long long size, long long n, void* stream) {
    return (int)fread(ptr, (size_t)size, (size_t)n, (FILE*)stream);
}

int c_fwrite_0(void* ptr, long long size, long long n, void* stream) {
    return (int)fwrite(ptr, (size_t)size, (size_t)n, (FILE*)stream);
}

int c_fflush_0(void* stream) {
    return fflush((FILE*)stream);
}

int c_fgetc_0(void* stream) {
    return fgetc((FILE*)stream);
}

void* get_stdin_0() { return stdin; }
void* get_stdout_0() { return stdout; }
void* get_stderr_0() { return stderr; }
