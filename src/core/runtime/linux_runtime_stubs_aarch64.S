/* Runtime provider stubs for AArch64 Linux ELF targets.
 *
 * These provide provider definitions for external symbols that Cheng programs
 * reference via @importc. The symbol names match the C ABI names.
 *
 * Each function is declared .globl so the ELF symbol table entry is
 * STB_GLOBAL, making it visible to cold_link_elf64_object_with_provider_archive
 * during archive-based linking.
 *
 * Assemble with:
 *   cc -target aarch64-unknown-linux-gnu -c -x assembler -o stubs.o $<
 * or:
 *   as -target aarch64-unknown-linux-gnu -o stubs.o $<
 */
.text
.p2align 2

/* ------------------------------------------------------------------ */
/* Atomic operations (used by std/atomic)                             */
/* ------------------------------------------------------------------ */

/* int32 cheng_atomic_cas_i32(ptr p, int32 expect, int32 desired)
   Returns 1 on success, 0 on failure (matches __sync_bool_compare_and_swap). */
.globl cheng_atomic_cas_i32
cheng_atomic_cas_i32:
    mov     w3, #1
.Lcas_loop_a64:
    ldaxr   w4, [x0]
    cmp     w4, w1
    b.ne    .Lcas_fail_a64
    stlxr   w5, w2, [x0]
    cbnz    w5, .Lcas_loop_a64
    mov     w0, w3
    ret
.Lcas_fail_a64:
    clrex
    mov     w0, wzr
    ret

/* void cheng_atomic_store_i32(ptr p, int32 val) */
.globl cheng_atomic_store_i32
cheng_atomic_store_i32:
    stlr    w1, [x0]
    ret

/* int32 cheng_atomic_load_i32(ptr p) */
.globl cheng_atomic_load_i32
cheng_atomic_load_i32:
    ldar    w0, [x0]
    ret

/* ------------------------------------------------------------------ */
/* Program entry and runtime support                                  */
/* ------------------------------------------------------------------ */

/* _start: ELF program entry point.  Sets up argc/argv from the
   initial stack pointer (per the AArch64 Linux ABI) and calls main.
   On return, exits via syscall. */
.globl _start
_start:
    ldr     x0, [sp]        /* argc = *sp */
    add     x1, sp, #8      /* argv = sp + 8 */
    stp     x0, x1, [sp, #-16]!
    bl      __cheng_setCmdLine
    bl      main
    mov     x8, #93         /* __NR_exit */
    svc     #0

/* __cheng_setCmdLine: called from _start or CRT startup to record
   command-line arguments for paramCount/paramStr builtins. */
.globl __cheng_setCmdLine
__cheng_setCmdLine:
    ret

/* cheng_program_argv_entry: primary entry called from _start.
   The Cheng-compiled primary .o provides a strong definition. */
.globl cheng_program_argv_entry
cheng_program_argv_entry:
    mov     w0, wzr
    ret

/* ------------------------------------------------------------------ */
/* Memory allocation                                                  */
/* ------------------------------------------------------------------ */

/* ptr cheng_host_malloc(int32 size) */
.globl cheng_host_malloc
cheng_host_malloc:
    mov     x0, #0          /* return NULL */
    ret

/* void cheng_host_free(ptr p) */
.globl cheng_host_free
cheng_host_free:
    ret

/* ptr cheng_host_realloc(ptr p, int32 size) */
.globl cheng_host_realloc
cheng_host_realloc:
    mov     x0, x1          /* return ptr as-is */
    ret

/* ptr cheng_malloc(int32 size) */
.globl cheng_malloc
cheng_malloc:
    mov     x0, #0
    ret

/* void cheng_free(ptr p) */
.globl cheng_free
cheng_free:
    ret

/* ------------------------------------------------------------------ */
/* Process exit                                                       */
/* ------------------------------------------------------------------ */

/* void cheng_host_exit_runtime(int32 code) */
.globl cheng_host_exit_runtime
cheng_host_exit_runtime:
    mov     x8, #93         /* __NR_exit */
    svc     #0

/* void cheng_host_exit_immediate_runtime(int32 code) */
.globl cheng_host_exit_immediate_runtime
cheng_host_exit_immediate_runtime:
    mov     x8, #93
    svc     #0

/* ------------------------------------------------------------------ */
/* Diagnostics / panic                                                */
/* ------------------------------------------------------------------ */

/* void cheng_panic_cstring_and_exit(const char *msg) */
.globl cheng_panic_cstring_and_exit
cheng_panic_cstring_and_exit:
    mov     x8, #93
    svc     #0

/* void cheng_debug_profile_flush_from_argv0(void) */
.globl cheng_debug_profile_flush_from_argv0
cheng_debug_profile_flush_from_argv0:
    ret

/* void cheng_native_register_line_map_from_argv0(void) */
.globl cheng_native_register_line_map_from_argv0
cheng_native_register_line_map_from_argv0:
    ret

/* void cheng_program_support_host_trace(void) */
.globl cheng_program_support_host_trace
cheng_program_support_host_trace:
    ret

/* int32 cheng_cstrlen(const char *s) */
.globl cheng_cstrlen
cheng_cstrlen:
    /* Simple strlen implementation */
    mov     x1, x0
.Lstrlen_loop:
    ldrb    w2, [x1], #1
    cbnz    w2, .Lstrlen_loop
    sub     x0, x1, x0
    sub     w0, w0, #1
    ret
