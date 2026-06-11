/* libshared/oeconnect/include/oeconnect/version.h */
#ifndef OEC_VERSION_H
#define OEC_VERSION_H

/*
 * Wire-protocol version. Must match spec/oec-protocol-v1.md §1.
 * CI fails if these drift from the spec.
 */
#define OEC_PROTOCOL_VERSION_MAJOR 1
#define OEC_PROTOCOL_VERSION_MINOR 0

/* Library implementation version (independent of wire version). */
#define OEC_LIB_VERSION_MAJOR 1
#define OEC_LIB_VERSION_MINOR 0
#define OEC_LIB_VERSION_PATCH 0

#define OEC_FRAME_MAGIC   0x3143454Fu /* 'O','E','C','1' little-endian */
#define OEC_REGION_MAGIC  0x5243454Fu /* 'O','E','C','R' little-endian */

#endif /* OEC_VERSION_H */
