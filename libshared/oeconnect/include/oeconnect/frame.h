/* libshared/oeconnect/include/oeconnect/frame.h */
#ifndef OEC_FRAME_H
#define OEC_FRAME_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Frame header (32 B, packed) ---- */
#pragma pack(push, 1)
typedef struct oec_frame_header {
    uint32_t magic;            /* OEC_FRAME_MAGIC */
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t stream_id;
    uint32_t payload_len;
    uint64_t sample_index;
    uint64_t host_qpc_ticks;
    uint16_t flags;            /* bit 0 = CONTINUATION, bit 1 = LOST_DATA */
    uint16_t crc16;            /* zero if disabled */
} oec_frame_header_t;
#pragma pack(pop)

/* sizeof must be exactly 32 bytes — static_assert in C11. */
OEC_STATIC_ASSERT(sizeof(oec_frame_header_t) == 32,
                  "oec_frame_header must be 32 bytes packed");

/* Frame flags */
#define OEC_FLAG_CONTINUATION 0x0001u
#define OEC_FLAG_LOST_DATA    0x0002u

/* Stream IDs */
#define OEC_STREAM_RAW_BLOCK       0x0001u
#define OEC_STREAM_FILTERED_BLOCK  0x0002u
#define OEC_STREAM_SPIKE           0x0003u
#define OEC_STREAM_TTL_EVENT       0x0004u
#define OEC_STREAM_SYNC            0x0010u
#define OEC_STREAM_CMD             0x0020u
#define OEC_STREAM_ACK             0x0021u
#define OEC_STREAM_ERROR           0x0022u
#define OEC_STREAM_HELLO           0x0030u

/* ---- Block sub-header (8 B, leads RAW_BLOCK / FILTERED_BLOCK payloads) ---- */
#pragma pack(push, 1)
typedef struct oec_block_subheader {
    uint16_t n_channels;
    uint16_t n_samples;
    uint8_t  dtype;            /* 0 = int16, 1 = float32, 2 = int32 (reserved) */
    uint8_t  source_id;
    uint16_t reserved;
} oec_block_subheader_t;
#pragma pack(pop)

OEC_STATIC_ASSERT(sizeof(oec_block_subheader_t) == 8,
                  "oec_block_subheader must be 8 bytes packed");

#define OEC_DTYPE_INT16   0
#define OEC_DTYPE_FLOAT32 1
#define OEC_DTYPE_INT32   2

/* ---- Command IDs (`CMD.cmd_id`) ---- */
#define OEC_CMD_START_RECORD 0x0001u
#define OEC_CMD_STOP_RECORD  0x0002u
#define OEC_CMD_SET_TTL      0x0003u
#define OEC_CMD_PULSE_TTL    0x0004u
#define OEC_CMD_START_ACQ    0x0005u
#define OEC_CMD_STOP_ACQ     0x0006u
#define OEC_CMD_GET_STATE    0x0007u

/* ---- Wire ACK status codes (distinct from oec_status_t) ---- */
#define OEC_ACK_OK            0u
#define OEC_ACK_PENDING       1u
#define OEC_ACK_COMPLETED     2u
#define OEC_ACK_BUSY          3u
#define OEC_ACK_NOT_SUPPORTED 4u
#define OEC_ACK_BAD_ARG       5u
#define OEC_ACK_TIMEOUT       6u
#define OEC_ACK_INTERNAL      7u

/* ---- Helpers ---- */

/* Fill a header with magic, version, supplied stream_id/payload_len/sample/qpc/flags.
 * crc16 is set to 0 (CRC disabled by default; producer may overwrite).
 */
OEC_API void oec_frame_init(
    oec_frame_header_t *h,
    uint16_t stream_id,
    uint32_t payload_len,
    uint64_t sample_index,
    uint64_t host_qpc_ticks,
    uint16_t flags);

/* Validate magic + version. Returns OEC_OK or an OEC_E_* code. */
OEC_API oec_status_t oec_frame_validate(const oec_frame_header_t *h);

/* CRC-16/CCITT-FALSE over `payload_len` bytes after the header. */
OEC_API uint16_t oec_crc16(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* OEC_FRAME_H */
