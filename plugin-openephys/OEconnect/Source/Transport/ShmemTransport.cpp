#include "ShmemTransport.h"
#include <cstring>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

namespace oec::plugin {

namespace {
uint64_t realtime_ns() {
#if defined(_WIN32)
    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; /* 100ns since 1601 */
    return t * 100ull;
#else
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}
}  // namespace

namespace {
constexpr uint32_t kSlotSize     = OEC_DEFAULT_SLOT_SIZE;
constexpr uint32_t kSlotCount    = OEC_DEFAULT_SLOT_COUNT;
constexpr uint32_t kCmdSlotSize  = OEC_DEFAULT_CMD_SLOT_SIZE;
constexpr uint32_t kCmdSlotCount = OEC_DEFAULT_CMD_SLOT_COUNT;
constexpr uint32_t kAckSlotSize  = OEC_DEFAULT_ACK_SLOT_SIZE;
constexpr uint32_t kAckSlotCount = OEC_DEFAULT_ACK_SLOT_COUNT;
}

ShmemTransport::ShmemTransport() = default;
ShmemTransport::~ShmemTransport() { stop(); }

bool ShmemTransport::start(const std::string& shm_name) {
    stop();
    name_ = shm_name;
    const size_t region_size = oec_region_size(
        kSlotSize, kSlotCount, kCmdSlotSize, kCmdSlotCount,
        kAckSlotSize, kAckSlotCount);
    if (region_size == 0) return false;

    // First try to open an existing region (consumer-side mount).
    if (oec_shm_open(shm_name.c_str(), region_size, &shm_,
                     &mapped_, &mapped_size_) == OEC_OK) {
        // Adopting an existing region: never trust its header blindly. Validate
        // magic, version and that the advertised layout fits what we mapped
        // before attaching — guards against a stale or hostile squatter on the
        // predictable region name.
        oec_region_header_t* hdr = nullptr;
        if (oec_region_open(mapped_, mapped_size_, &hdr) != OEC_OK) { stop(); return false; }
        region_header_ = hdr;
    } else {
        // Create fresh.
        if (oec_shm_create(shm_name.c_str(), region_size, /*truncate=*/1,
                           &shm_, &mapped_, &mapped_size_) != OEC_OK) {
            return false;
        }
        if (oec_region_init(mapped_, mapped_size_,
                            kSlotSize, kSlotCount,
                            kCmdSlotSize, kCmdSlotCount,
                            kAckSlotSize, kAckSlotCount) != OEC_OK) {
            stop();
            return false;
        }
        region_header_ = reinterpret_cast<oec_region_header_t*>(mapped_);
    }
    if (oec_ringbuf_attach(mapped_, OEC_RING_DATA, &data_ring_) != OEC_OK) { stop(); return false; }
    if (oec_ringbuf_attach(mapped_, OEC_RING_CMD,  &cmd_ring_)  != OEC_OK) { stop(); return false; }
    if (oec_ringbuf_attach(mapped_, OEC_RING_ACK,  &ack_ring_)  != OEC_OK) { stop(); return false; }
    return true;
}

void ShmemTransport::stop() {
    if (data_ring_) { oec_ringbuf_detach(data_ring_); data_ring_ = nullptr; }
    if (cmd_ring_)  { oec_ringbuf_detach(cmd_ring_);  cmd_ring_  = nullptr; }
    if (ack_ring_)  { oec_ringbuf_detach(ack_ring_);  ack_ring_  = nullptr; }
    if (shm_) { oec_shm_close(shm_); shm_ = nullptr; mapped_ = nullptr; mapped_size_ = 0; }
    region_header_ = nullptr;
}

uint8_t* ShmemTransport::acquireDataSlot(uint32_t* out_cap, bool dropOldest) {
    if (!data_ring_) return nullptr;
    void* slot = oec_ringbuf_acquire(data_ring_, dropOldest ? 1 : 0, out_cap);

    /* With dropOldest the ring never refuses a slot -- it evicts the oldest unread
     * frame and hands one back. That eviction is the drop, and it is only visible
     * through the ring's own counter, so poll it here rather than testing `slot`. */
    const uint64_t evictions = oec_ringbuf_evictions(data_ring_);
    for (uint64_t i = last_evictions_; i < evictions; ++i) noteDropped();
    last_evictions_ = evictions;

    return (uint8_t*)slot;
}
void ShmemTransport::publishData(uint32_t) {
    oec_ringbuf_publish(data_ring_);
    /* Refresh the producer liveness stamp so consumers can tell this session
     * is alive (spec §4.6 discovery freshness). Off the SPSC hot indices. */
    if (region_header_) region_header_->producer_heartbeat_ns = realtime_ns();
}

uint8_t* ShmemTransport::acquireAckSlot(uint32_t* out_cap) {
    if (!ack_ring_) return nullptr;
    return (uint8_t*)oec_ringbuf_acquire(ack_ring_, /*dropOldest=*/0, out_cap);
}
void ShmemTransport::publishAck(uint32_t) { oec_ringbuf_publish(ack_ring_); }

const uint8_t* ShmemTransport::peekCmd(uint32_t* out_size) {
    if (!cmd_ring_) return nullptr;
    return (const uint8_t*)oec_ringbuf_peek(cmd_ring_, out_size);
}
void ShmemTransport::consumeCmd() { oec_ringbuf_consume(cmd_ring_); }

}  // namespace oec::plugin
