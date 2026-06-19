/* libshared/oeconnect/include/oeconnect/types.h */
#ifndef OEC_TYPES_H
#define OEC_TYPES_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #define OEC_EXPORT __declspec(dllexport)
  #define OEC_IMPORT __declspec(dllimport)
#else
  #define OEC_EXPORT __attribute__((visibility("default")))
  #define OEC_IMPORT
#endif

#if defined(OEC_BUILDING_LIB)
  #define OEC_API OEC_EXPORT
#elif defined(OEC_STATIC)
  /* Consumer statically links liboeconnect into its own module: no dllimport. */
  #define OEC_API
#else
  #define OEC_API OEC_IMPORT
#endif

#define OEC_CACHELINE 64
#if defined(_MSC_VER)
  #define OEC_ALIGNAS(n) __declspec(align(n))
#else
  #define OEC_ALIGNAS(n) __attribute__((aligned(n)))
#endif

/* C/C++ portability shim for compile-time assertions.
 * C11 has `_Static_assert`, C++11+ has `static_assert` — pick the right one. */
#ifdef __cplusplus
  #define OEC_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
  #define OEC_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Library-wide status codes. Distinct from the wire `ACK.status` codes. */
typedef enum oec_status {
    OEC_OK                       = 0,
    OEC_E_INVALID_ARG            = -1,
    OEC_E_BAD_MAGIC              = -2,
    OEC_E_VERSION_MISMATCH       = -3,
    OEC_E_FRAME_TOO_LARGE        = -4,
    OEC_E_RING_FULL              = -5,
    OEC_E_RING_EMPTY             = -6,
    OEC_E_SYSCALL                = -7,
    OEC_E_NO_SESSION             = -8,
    OEC_E_PARSE                  = -9,
    OEC_E_NOT_IMPLEMENTED        = -10
} oec_status_t;

#ifdef __cplusplus
}
#endif

#endif /* OEC_TYPES_H */
