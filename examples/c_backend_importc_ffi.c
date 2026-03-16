#include "chengbase.h"

#if defined(_MSC_VER)
#  define FFI_ALIGN_PREFIX(x) __declspec(align(x))
#  define FFI_ALIGN_SUFFIX(x)
#  define FFI_PACKED_BEGIN __pragma(pack(push, 1))
#  define FFI_PACKED_END __pragma(pack(pop))
#  define FFI_PACKED_ATTR
#else
#  define FFI_ALIGN_PREFIX(x)
#  define FFI_ALIGN_SUFFIX(x) __attribute__((aligned(x)))
#  define FFI_PACKED_BEGIN
#  define FFI_PACKED_END
#  define FFI_PACKED_ATTR __attribute__((packed))
#endif

FFI_PACKED_BEGIN
FFI_ALIGN_PREFIX(1) typedef struct FFI_PACKED_ATTR PackedPair {
  NI8 a;
  NI32 b;
} PackedPair;
FFI_PACKED_END

FFI_ALIGN_PREFIX(8) typedef struct AlignedPair {
  NI8 a;
  NI32 b;
  NI64 c;
} AlignedPair FFI_ALIGN_SUFFIX(8);

N_CDECL(PackedPair, ffi_make_packed)(NI8 a, NI32 b) {
  PackedPair p;
  p.a = a;
  p.b = b;
  return p;
}

N_CDECL(AlignedPair, ffi_make_aligned)(NI8 a, NI32 b, NI64 c) {
  AlignedPair q;
  q.a = a;
  q.b = b;
  q.c = c;
  return q;
}
