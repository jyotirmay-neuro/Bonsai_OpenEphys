/* libshared/oeconnect/include/oeconnect/hello.h */
#ifndef OEC_HELLO_H
#define OEC_HELLO_H

#include "oeconnect/types.h"
#include "oeconnect/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct oec_hello_body {
    uint16_t protocol_major;
    uint16_t protocol_minor;
    uint32_t plugin_version;   /* (M<<16)|(m<<8)|p */
    uint32_t lib_version;
    uint32_t reserved;
} oec_hello_body_t;
#pragma pack(pop)

OEC_STATIC_ASSERT(sizeof(oec_hello_body_t) == 16,
                  "HELLO body must be 16 bytes (spec v1.1 §8)");

/* Writes header+body into dest. Returns bytes written, or 0 if cap is too small. */
OEC_API size_t oec_hello_emit(uint8_t *dest, size_t cap,
                              uint32_t plugin_version,
                              uint32_t lib_version);

#ifdef __cplusplus
}
#endif

#endif /* OEC_HELLO_H */
