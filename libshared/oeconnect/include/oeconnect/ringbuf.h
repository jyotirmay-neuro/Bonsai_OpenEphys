/* libshared/oeconnect/include/oeconnect/ringbuf.h */
#ifndef OEC_RINGBUF_H
#define OEC_RINGBUF_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Region header (4 KiB, first thing in the shmem region) ---- */
#pragma pack(push, 8)
typedef struct oec_region_header {
    uint32_t magic;                       /* OEC_REGION_MAGIC */
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t slot_size;
    uint32_t slot_count;
    uint32_t cmd_slot_size;
    uint32_t cmd_slot_count;
    uint32_t ack_slot_size;
    uint32_t ack_slot_count;
    uint32_t _pad0;
    uint64_t producer_heartbeat_ns;
    uint64_t fpga_sample_rate_hz_x1000;
    uint8_t  reserved[4096 - 56];         /* pad to 4 KiB */
} oec_region_header_t;
#pragma pack(pop)

OEC_STATIC_ASSERT(sizeof(oec_region_header_t) == 4096,
                  "region header must be 4 KiB");

/* Default sizing (matches spec §5.7) */
#define OEC_DEFAULT_SLOT_SIZE       65536u
#define OEC_DEFAULT_SLOT_COUNT      256u
#define OEC_DEFAULT_CMD_SLOT_SIZE   4096u
#define OEC_DEFAULT_CMD_SLOT_COUNT  64u
#define OEC_DEFAULT_ACK_SLOT_SIZE   4096u
#define OEC_DEFAULT_ACK_SLOT_COUNT  64u

/* ---- Opaque ringbuffer handle (one per direction) ---- */
typedef struct oec_ringbuf oec_ringbuf_t;

/*
 * Compute total bytes a region requires for the given sizing parameters.
 * Layout: header (4 KiB) + (data prod+cons idx, 128 B) + data ring +
 *         (cmd prod+cons idx, 128 B) + cmd ring +
 *         (ack prod+cons idx, 128 B) + ack ring.
 * Returns 0 on overflow / invalid params.
 */
OEC_API size_t oec_region_size(
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count);

/*
 * Initialise a region (producer side, called once when shm is created).
 * `mem` must point to `oec_region_size(...)` zero-initialised bytes.
 */
OEC_API oec_status_t oec_region_init(
    void *mem, size_t mem_len,
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count);

/*
 * Validate an existing region (consumer side after mapping).
 * Sets *out_header to a pointer into `mem`.
 */
OEC_API oec_status_t oec_region_open(
    void *mem, size_t mem_len,
    oec_region_header_t **out_header);

/* ---- Per-ring handles. Each handle picks one of {data, cmd, ack}. ---- */
typedef enum oec_ring_kind {
    OEC_RING_DATA = 0,
    OEC_RING_CMD  = 1,
    OEC_RING_ACK  = 2
} oec_ring_kind_t;

OEC_API oec_status_t oec_ringbuf_attach(
    void *region_mem, oec_ring_kind_t kind, oec_ringbuf_t **out);

OEC_API void oec_ringbuf_detach(oec_ringbuf_t *rb);

/* ---- Producer API (wait-free) ---- */

/* Acquire a writable slot. Returns NULL if the ring is full and `drop_oldest`
 * is false. If `drop_oldest` is true, advances the consumer index to drop the
 * oldest slot and always returns a writable pointer.
 * `*out_slot_size` returns the slot byte size (capacity). */
OEC_API void *oec_ringbuf_acquire(oec_ringbuf_t *rb, int drop_oldest, uint32_t *out_slot_size);

/* Publish the slot most recently acquired (release ordering). */
OEC_API void oec_ringbuf_publish(oec_ringbuf_t *rb);

/* ---- Consumer API (wait-free) ---- */

/* Peek the oldest unconsumed slot. NULL if empty. */
OEC_API const void *oec_ringbuf_peek(oec_ringbuf_t *rb, uint32_t *out_slot_size);

/* Mark the peeked slot consumed (release ordering). */
OEC_API void oec_ringbuf_consume(oec_ringbuf_t *rb);

/* Diagnostics. */

/* Number of slots this producer handle has evicted because the ring was full.
 * `oec_ringbuf_acquire(drop_oldest=1)` always returns a writable slot, so this is
 * the only way a producer learns that it overwrote unread data. Producer-local:
 * counts evictions performed through THIS handle, not a shared total. */
OEC_API uint64_t oec_ringbuf_evictions(const oec_ringbuf_t *rb);

OEC_API uint64_t oec_ringbuf_producer_index(const oec_ringbuf_t *rb);
OEC_API uint64_t oec_ringbuf_consumer_index(const oec_ringbuf_t *rb);

#ifdef __cplusplus
}
#endif

#endif /* OEC_RINGBUF_H */
