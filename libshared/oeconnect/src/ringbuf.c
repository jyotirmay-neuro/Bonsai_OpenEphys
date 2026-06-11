/* libshared/oeconnect/src/ringbuf.c */
#define OEC_BUILDING_LIB
#include "oeconnect/ringbuf.h"
#include "oeconnect/version.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/*
 * Region layout:
 *   [region_header (4 KiB)]
 *   [data_prod_idx (64 B)] [data_cons_idx (64 B)] [data_ring]
 *   [cmd_prod_idx  (64 B)] [cmd_cons_idx  (64 B)] [cmd_ring]
 *   [ack_prod_idx  (64 B)] [ack_cons_idx  (64 B)] [ack_ring]
 * Each idx pair gets its own cache line to avoid false sharing.
 */

#define IDX_PAIR_BYTES 128u   /* 2 × OEC_CACHELINE */

typedef struct ring_layout {
    size_t prod_off;
    size_t cons_off;
    size_t ring_off;
    uint32_t slot_size;
    uint32_t slot_count;
} ring_layout_t;

struct oec_ringbuf {
    uint8_t *base;
    ring_layout_t lo;
    /* cached for producer/consumer fast paths */
    uint64_t cached_prod;
    uint64_t cached_cons;
};

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static int compute_layout(
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count,
    ring_layout_t *data, ring_layout_t *cmd, ring_layout_t *ack,
    size_t *total)
{
    if (!slot_size || !slot_count) return -1;
    if (!cmd_slot_size || !cmd_slot_count) return -1;
    if (!ack_slot_size || !ack_slot_count) return -1;
    if ((slot_count & (slot_count - 1)) != 0) return -1;  /* require power of two */
    if ((cmd_slot_count & (cmd_slot_count - 1)) != 0) return -1;
    if ((ack_slot_count & (ack_slot_count - 1)) != 0) return -1;

    size_t off = sizeof(oec_region_header_t);  /* 4096 */
    data->prod_off = off; off += OEC_CACHELINE;
    data->cons_off = off; off += OEC_CACHELINE;
    data->ring_off = off; off += (size_t)slot_size * slot_count;
    data->slot_size = slot_size;
    data->slot_count = slot_count;

    off = align_up(off, OEC_CACHELINE);
    cmd->prod_off = off; off += OEC_CACHELINE;
    cmd->cons_off = off; off += OEC_CACHELINE;
    cmd->ring_off = off; off += (size_t)cmd_slot_size * cmd_slot_count;
    cmd->slot_size = cmd_slot_size;
    cmd->slot_count = cmd_slot_count;

    off = align_up(off, OEC_CACHELINE);
    ack->prod_off = off; off += OEC_CACHELINE;
    ack->cons_off = off; off += OEC_CACHELINE;
    ack->ring_off = off; off += (size_t)ack_slot_size * ack_slot_count;
    ack->slot_size = ack_slot_size;
    ack->slot_count = ack_slot_count;

    *total = align_up(off, OEC_CACHELINE);
    return 0;
}

size_t oec_region_size(
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count)
{
    ring_layout_t d, c, a;
    size_t total = 0;
    if (compute_layout(slot_size, slot_count,
                       cmd_slot_size, cmd_slot_count,
                       ack_slot_size, ack_slot_count,
                       &d, &c, &a, &total) != 0) return 0;
    return total;
}

oec_status_t oec_region_init(
    void *mem, size_t mem_len,
    uint32_t slot_size, uint32_t slot_count,
    uint32_t cmd_slot_size, uint32_t cmd_slot_count,
    uint32_t ack_slot_size, uint32_t ack_slot_count)
{
    if (!mem) return OEC_E_INVALID_ARG;
    ring_layout_t d, c, a;
    size_t total = 0;
    if (compute_layout(slot_size, slot_count,
                       cmd_slot_size, cmd_slot_count,
                       ack_slot_size, ack_slot_count,
                       &d, &c, &a, &total) != 0) return OEC_E_INVALID_ARG;
    if (mem_len < total) return OEC_E_INVALID_ARG;

    memset(mem, 0, total);
    oec_region_header_t *h = (oec_region_header_t *)mem;
    h->magic = OEC_REGION_MAGIC;
    h->version_major = OEC_PROTOCOL_VERSION_MAJOR;
    h->version_minor = OEC_PROTOCOL_VERSION_MINOR;
    h->slot_size = slot_size;
    h->slot_count = slot_count;
    h->cmd_slot_size = cmd_slot_size;
    h->cmd_slot_count = cmd_slot_count;
    h->ack_slot_size = ack_slot_size;
    h->ack_slot_count = ack_slot_count;
    return OEC_OK;
}

oec_status_t oec_region_open(void *mem, size_t mem_len, oec_region_header_t **out_header)
{
    if (!mem || !out_header) return OEC_E_INVALID_ARG;
    if (mem_len < sizeof(oec_region_header_t)) return OEC_E_INVALID_ARG;
    oec_region_header_t *h = (oec_region_header_t *)mem;
    if (h->magic != OEC_REGION_MAGIC) return OEC_E_BAD_MAGIC;
    if (h->version_major != OEC_PROTOCOL_VERSION_MAJOR) return OEC_E_VERSION_MISMATCH;
    *out_header = h;
    return OEC_OK;
}

oec_status_t oec_ringbuf_attach(void *region_mem, oec_ring_kind_t kind, oec_ringbuf_t **out)
{
    if (!region_mem || !out) return OEC_E_INVALID_ARG;
    oec_region_header_t *h = (oec_region_header_t *)region_mem;
    if (h->magic != OEC_REGION_MAGIC) return OEC_E_BAD_MAGIC;

    ring_layout_t d, c, a;
    size_t total = 0;
    if (compute_layout(h->slot_size, h->slot_count,
                       h->cmd_slot_size, h->cmd_slot_count,
                       h->ack_slot_size, h->ack_slot_count,
                       &d, &c, &a, &total) != 0) return OEC_E_INVALID_ARG;

    oec_ringbuf_t *rb = (oec_ringbuf_t *)calloc(1, sizeof(*rb));
    if (!rb) return OEC_E_SYSCALL;
    rb->base = (uint8_t *)region_mem;
    switch (kind) {
        case OEC_RING_DATA: rb->lo = d; break;
        case OEC_RING_CMD:  rb->lo = c; break;
        case OEC_RING_ACK:  rb->lo = a; break;
        default: free(rb); return OEC_E_INVALID_ARG;
    }
    *out = rb;
    return OEC_OK;
}

void oec_ringbuf_detach(oec_ringbuf_t *rb) { free(rb); }

static inline _Atomic uint64_t *prod_idx(oec_ringbuf_t *rb) {
    return (_Atomic uint64_t *)(rb->base + rb->lo.prod_off);
}
static inline _Atomic uint64_t *cons_idx(oec_ringbuf_t *rb) {
    return (_Atomic uint64_t *)(rb->base + rb->lo.cons_off);
}

void *oec_ringbuf_acquire(oec_ringbuf_t *rb, int drop_oldest, uint32_t *out_slot_size)
{
    if (!rb) return NULL;
    uint64_t p = atomic_load_explicit(prod_idx(rb), memory_order_relaxed);
    uint64_t c = atomic_load_explicit(cons_idx(rb), memory_order_acquire);
    if (p - c >= rb->lo.slot_count) {
        if (!drop_oldest) return NULL;
        atomic_store_explicit(cons_idx(rb), c + 1, memory_order_release);
        c = c + 1;
    }
    uint8_t *slot = rb->base + rb->lo.ring_off
                  + (size_t)(p % rb->lo.slot_count) * rb->lo.slot_size;
    if (out_slot_size) *out_slot_size = rb->lo.slot_size;
    rb->cached_prod = p;
    return slot;
}

void oec_ringbuf_publish(oec_ringbuf_t *rb)
{
    if (!rb) return;
    atomic_store_explicit(prod_idx(rb), rb->cached_prod + 1, memory_order_release);
}

const void *oec_ringbuf_peek(oec_ringbuf_t *rb, uint32_t *out_slot_size)
{
    if (!rb) return NULL;
    uint64_t c = atomic_load_explicit(cons_idx(rb), memory_order_relaxed);
    uint64_t p = atomic_load_explicit(prod_idx(rb), memory_order_acquire);
    if (p == c) return NULL;
    uint8_t *slot = rb->base + rb->lo.ring_off
                  + (size_t)(c % rb->lo.slot_count) * rb->lo.slot_size;
    if (out_slot_size) *out_slot_size = rb->lo.slot_size;
    rb->cached_cons = c;
    return slot;
}

void oec_ringbuf_consume(oec_ringbuf_t *rb)
{
    if (!rb) return;
    atomic_store_explicit(cons_idx(rb), rb->cached_cons + 1, memory_order_release);
}

uint64_t oec_ringbuf_producer_index(const oec_ringbuf_t *rb) {
    return atomic_load_explicit((_Atomic uint64_t *)(rb->base + rb->lo.prod_off), memory_order_relaxed);
}
uint64_t oec_ringbuf_consumer_index(const oec_ringbuf_t *rb) {
    return atomic_load_explicit((_Atomic uint64_t *)(rb->base + rb->lo.cons_off), memory_order_relaxed);
}
