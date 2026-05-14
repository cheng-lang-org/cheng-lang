/* AArch64 Linux provider object for cold linkerless atomic smoke tests.
 *
 * This file intentionally exports only the concrete atomic C ABI primitives
 * used by src/tests/atomic_i32_direct_runtime_smoke.cheng. It must not define
 * allocator, entry, panic, or host-runtime support.
 */
.text
.p2align 2

/* int32 cheng_atomic_cas_i32(ptr p, int32 expect, int32 desired)
   Returns 1 on success, 0 on failure. */
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
