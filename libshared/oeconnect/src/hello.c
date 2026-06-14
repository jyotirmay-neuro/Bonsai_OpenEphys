/* libshared/oeconnect/src/hello.c */
#define OEC_BUILDING_LIB
#include "oeconnect/hello.h"
#include "oeconnect/version.h"

#include <string.h>

size_t oec_hello_emit(uint8_t *dest, size_t cap,
                      uint32_t plugin_version, uint32_t lib_version)
{
    const size_t total = sizeof(oec_frame_header_t) + sizeof(oec_hello_body_t);
    if (!dest || cap < total) return 0;
    oec_frame_init((oec_frame_header_t *)dest, OEC_STREAM_HELLO,
                   (uint32_t)sizeof(oec_hello_body_t), 0, 0, 0);
    oec_hello_body_t body;
    body.protocol_major = OEC_PROTOCOL_VERSION_MAJOR;
    body.protocol_minor = OEC_PROTOCOL_VERSION_MINOR;
    body.plugin_version = plugin_version;
    body.lib_version    = lib_version;
    body.reserved       = 0;
    memcpy(dest + sizeof(oec_frame_header_t), &body, sizeof(body));
    return total;
}
