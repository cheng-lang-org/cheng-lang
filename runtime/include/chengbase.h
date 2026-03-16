#ifndef CHENGBASE_H
#define CHENGBASE_H

#ifdef __cplusplus
#  if __cplusplus >= 201103L
#    define NIL nullptr
#  else
#    define NIL 0
#  endif
#else
#  include <stdbool.h>
#  define NIL NULL
#endif

#ifdef __cplusplus
#  define NB8 bool
#elif (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901)
#  define NB8 _Bool
#else
typedef unsigned char NB8;
#endif

typedef unsigned char NC8;

typedef float NF32;
typedef double NF64;

#if defined(__BORLANDC__) || defined(_MSC_VER)
typedef signed char NI8;
typedef signed short int NI16;
typedef signed int NI32;
typedef __int64 NI64;
typedef unsigned char NU8;
typedef unsigned short int NU16;
typedef unsigned int NU32;
typedef unsigned __int64 NU64;
#elif defined(HAVE_STDINT_H)
#  ifndef USE_CHENG_NAMESPACE
#    include <stdint.h>
#  endif
typedef int8_t NI8;
typedef int16_t NI16;
typedef int32_t NI32;
typedef int64_t NI64;
typedef uint8_t NU8;
typedef uint16_t NU16;
typedef uint32_t NU32;
typedef uint64_t NU64;
#elif defined(HAVE_CSTDINT)
#  ifndef USE_CHENG_NAMESPACE
#    include <cstdint>
#  endif
typedef std::int8_t NI8;
typedef std::int16_t NI16;
typedef std::int32_t NI32;
typedef std::int64_t NI64;
typedef std::uint8_t NU8;
typedef std::uint16_t NU16;
typedef std::uint32_t NU32;
typedef std::uint64_t NU64;
#else
#  ifdef __INT8_TYPE__
typedef __INT8_TYPE__ NI8;
#  else
typedef signed char NI8;
#  endif
#  ifdef __INT16_TYPE__
typedef __INT16_TYPE__ NI16;
#  else
typedef signed short int NI16;
#  endif
#  ifdef __INT32_TYPE__
typedef __INT32_TYPE__ NI32;
#  else
typedef signed int NI32;
#  endif
#  ifdef __INT64_TYPE__
typedef __INT64_TYPE__ NI64;
#  else
typedef signed long long int NI64;
#  endif
#  ifdef __UINT8_TYPE__
typedef __UINT8_TYPE__ NU8;
#  else
typedef unsigned char NU8;
#  endif
#  ifdef __UINT16_TYPE__
typedef __UINT16_TYPE__ NU16;
#  else
typedef unsigned short int NU16;
#  endif
#  ifdef __UINT32_TYPE__
typedef __UINT32_TYPE__ NU32;
#  else
typedef unsigned int NU32;
#  endif
#  ifdef __UINT64_TYPE__
typedef __UINT64_TYPE__ NU64;
#  else
typedef unsigned long long int NU64;
#  endif
#endif

#ifndef TYPE_ALIASES
#define TYPE_ALIASES 1
typedef NI8 int8;
typedef NI16 int16;
typedef NI32 int32;
typedef NI64 int64;
typedef NU8 uint8;
typedef NU16 uint16;
typedef NU32 uint32;
typedef NU64 uint64;
#endif

#ifdef INTBITS
#  if INTBITS == 64
typedef NI64 NI;
typedef NU64 NU;
#  elif INTBITS == 32
typedef NI32 NI;
typedef NU32 NU;
#  elif INTBITS == 16
typedef NI16 NI;
typedef NU16 NU;
#  elif INTBITS == 8
typedef NI8 NI;
typedef NU8 NU;
#  else
#    error "invalid bit width for int"
#  endif
#endif

#define TRUE true
#define FALSE false

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <math.h>

#ifndef NAN
#  ifndef _HUGE_ENUF
#    define _HUGE_ENUF  1e+300
#  endif
#  define NAN_INFINITY ((float)(_HUGE_ENUF * _HUGE_ENUF))
#  define NAN ((float)(NAN_INFINITY * 0.0F))
#endif

#ifndef INF
#  ifdef INFINITY
#    define INF INFINITY
#  elif defined(HUGE_VAL)
#    define INF  HUGE_VAL
#  elif defined(_MSC_VER)
#    include <float.h>
#    define INF (DBL_MAX+DBL_MAX)
#  else
#    define INF (1.0 / 0.0)
#  endif
#endif

#ifndef zeroMem_0
#define zeroMem_0 zeroMemQ_Q_ptrQ_voidQ_intQ_Q_retQ_node_0__ovl0
#endif

#ifndef RESULT_HELPERS
#define RESULT_HELPERS 1
#  ifdef IsOk
#    undef IsOk
#  endif
#  define IsOk(r) ((r).ok)
#  ifdef IsErr
#    undef IsErr
#  endif
#  define IsErr(r) (!(r).ok)
#  ifdef Value
#    undef Value
#  endif
#  define Value(r) ((r).value)
#  ifdef Error
#    undef Error
#  endif
#  define Error(r) ((r).err.msg)
#endif

static inline NB8 setHas_0(NU64 setVal, NU64 idx) {
  if (idx >= 64) {
    return FALSE;
  }
  return (setVal & (NU64)(1ULL << idx)) != 0;
}

static inline NU64 setMask_0(NU64 idx) {
  if (idx >= 64) {
    return (NU64)0;
  }
  return (NU64)(1ULL << idx);
}

#if defined(__GNUC__) || defined(_MSC_VER)
#  define IL64(x) x##LL
#else
#  define IL64(x) ((NI64)x)
#endif

#ifdef  __cplusplus
#  define EXTERNC extern "C"
#else
#  define EXTERNC
#endif

#if defined(WIN32) || defined(_WIN32)
#  define N_LIB_PRIVATE
#  define N_CDECL(rettype, name) rettype __cdecl name
#  define N_STDCALL(rettype, name) rettype __stdcall name
#  define N_SYSCALL(rettype, name) rettype __syscall name
#  define N_FASTCALL(rettype, name) rettype __fastcall name
#  define N_THISCALL(rettype, name) rettype __thiscall name
#  define N_SAFECALL(rettype, name) rettype __stdcall name
#  define N_CDECL_PTR(rettype, name) rettype (__cdecl *name)
#  define N_STDCALL_PTR(rettype, name) rettype (__stdcall *name)
#  define N_SYSCALL_PTR(rettype, name) rettype (__syscall *name)
#  define N_FASTCALL_PTR(rettype, name) rettype (__fastcall *name)
#  define N_THISCALL_PTR(rettype, name) rettype (__thiscall *name)
#  define N_SAFECALL_PTR(rettype, name) rettype (__stdcall *name)
#  ifdef __EMSCRIPTEN__
#    define N_LIB_EXPORT  EXTERNC __declspec(dllexport) __attribute__((used))
#    define N_LIB_EXPORT_VAR  __declspec(dllexport) __attribute__((used))
#  else
#    define N_LIB_EXPORT  EXTERNC __declspec(dllexport)
#    define N_LIB_EXPORT_VAR  __declspec(dllexport)
#  endif
#  define N_LIB_IMPORT  extern __declspec(dllimport)
#else
#  define N_LIB_PRIVATE __attribute__((visibility("hidden")))
#  if defined(__GNUC__)
#    define N_CDECL(rettype, name) rettype name
#    define N_STDCALL(rettype, name) rettype name
#    define N_SYSCALL(rettype, name) rettype name
#    define N_FASTCALL(rettype, name) __attribute__((fastcall)) rettype name
#    define N_SAFECALL(rettype, name) rettype name
#    define N_CDECL_PTR(rettype, name) rettype (*name)
#    define N_STDCALL_PTR(rettype, name) rettype (*name)
#    define N_SYSCALL_PTR(rettype, name) rettype (*name)
#    define N_FASTCALL_PTR(rettype, name) __attribute__((fastcall)) rettype (*name)
#    define N_SAFECALL_PTR(rettype, name) rettype (*name)
#  else
#    define N_CDECL(rettype, name) rettype name
#    define N_STDCALL(rettype, name) rettype name
#    define N_SYSCALL(rettype, name) rettype name
#    define N_FASTCALL(rettype, name) rettype name
#    define N_SAFECALL(rettype, name) rettype name
#    define N_CDECL_PTR(rettype, name) rettype (*name)
#    define N_STDCALL_PTR(rettype, name) rettype (*name)
#    define N_SYSCALL_PTR(rettype, name) rettype (*name)
#    define N_FASTCALL_PTR(rettype, name) rettype (*name)
#    define N_SAFECALL_PTR(rettype, name) rettype (*name)
#  endif
#  ifdef __EMSCRIPTEN__
#    define N_LIB_EXPORT EXTERNC __attribute__((visibility("default"), used))
#    define N_LIB_EXPORT_VAR  __attribute__((visibility("default"), used))
#  else
#    define N_LIB_EXPORT EXTERNC __attribute__((visibility("default")))
#    define N_LIB_EXPORT_VAR  __attribute__((visibility("default")))
#  endif
#  define N_LIB_IMPORT  extern
#endif

#if defined(__BORLANDC__) || defined(_MSC_VER) || defined(WIN32) || defined(_WIN32)
#  define N_CHENGCALL(rettype, name) rettype __fastcall name
#  define N_CHENGCALL_PTR(rettype, name) rettype (__fastcall *name)
#else
#  define N_CHENGCALL(rettype, name) rettype name
#  define N_CHENGCALL_PTR(rettype, name) rettype (*name)
#endif

#ifndef C_CHENGCALL
#  define C_CHENGCALL(rettype, name) N_CHENGCALL(rettype, name)
#endif
#ifndef C_CHENGCALL_PTR
#  define C_CHENGCALL_PTR(rettype, name) N_CHENGCALL_PTR(rettype, name)
#endif

#define N_NOCONV(rettype, name) rettype name
#define N_NOCONV_PTR(rettype, name) rettype (*name)

#if defined(__GNUC__) || defined(__TINYC__)
#  define N_INLINE(rettype, name) inline rettype name
#elif defined(__BORLANDC__) || defined(_MSC_VER)
#  define N_INLINE(rettype, name) __inline rettype name
#else
#  define N_INLINE(rettype, name) rettype __inline name
#endif

#define N_INLINE_PTR(rettype, name) rettype (*name)

#if defined(__GNUC__) || defined(__ICC__)
#  define N_NOINLINE __attribute__((__noinline__))
#elif defined(_MSC_VER)
#  define N_NOINLINE __declspec(noinline)
#else
#  define N_NOINLINE
#endif

#define N_NOINLINE_PTR(rettype, name) rettype (*name)

#if defined(_MSC_VER)
#  define ALIGN(x)  __declspec(align(x))
#  define ALIGNOF(x) __alignof(x)
#else
#  define ALIGN(x)  __attribute__((aligned(x)))
#  define ALIGNOF(x) __alignof__(x)
#endif

#endif /* CHENGBASE_H */
