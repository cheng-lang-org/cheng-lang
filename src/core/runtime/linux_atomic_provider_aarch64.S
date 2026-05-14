/* AArch64 Linux provider object for cold linkerless atomic smoke tests.
 *
 * This file intentionally exports only the concrete atomic primitives used by
 * src/tests/atomic_i32_direct_runtime_smoke.cheng. It is not a runtime fallback
 * archive and must not define allocator, entry, panic, or host-runtime stubs.
 */
.text
.p2align 2

/* int32 atomicCasRaw(ptr p, int32 expect, int32 desired)
   Returns 1 on success, 0 on failure. */
.globl atomicCasRaw
atomicCasRaw:
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

/* void atomicStoreRaw(ptr p, int32 val) */
.globl atomicStoreRaw
atomicStoreRaw:
    stlr    w1, [x0]
    ret

/* int32 atomicLoadRaw(ptr p) */
.globl atomicLoadRaw
atomicLoadRaw:
    ldar    w0, [x0]
    ret
