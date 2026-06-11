/* libshared/oeconnect/src/frame.c */
#define OEC_BUILDING_LIB
#include "oeconnect/frame.h"
#include "oeconnect/version.h"

#include <string.h>

void oec_frame_init(
    oec_frame_header_t *h,
    uint16_t stream_id,
    uint32_t payload_len,
    uint64_t sample_index,
    uint64_t host_qpc_ticks,
    uint16_t flags)
{
    if (!h) return;
    h->magic = OEC_FRAME_MAGIC;
    h->version_major = OEC_PROTOCOL_VERSION_MAJOR;
    h->version_minor = OEC_PROTOCOL_VERSION_MINOR;
    h->stream_id = stream_id;
    h->payload_len = payload_len;
    h->sample_index = sample_index;
    h->host_qpc_ticks = host_qpc_ticks;
    h->flags = flags;
    h->crc16 = 0;
}

oec_status_t oec_frame_validate(const oec_frame_header_t *h)
{
    if (!h) return OEC_E_INVALID_ARG;
    if (h->magic != OEC_FRAME_MAGIC) return OEC_E_BAD_MAGIC;
    if (h->version_major != OEC_PROTOCOL_VERSION_MAJOR) return OEC_E_VERSION_MISMATCH;
    /* minor mismatch is forward-compatible */
    return OEC_OK;
}

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout */
uint16_t oec_crc16(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}
