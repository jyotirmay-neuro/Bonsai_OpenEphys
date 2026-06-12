#include "ShmemTransport.h"
#include <cstring>

namespace oec::plugin {

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
                     &mapped_, &mapped_size_) != OEC_OK) {
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
}

uint8_t* ShmemTransport::acquireDataSlot(uint32_t* out_cap, bool dropOldest) {
    if (!data_ring_) return nullptr;
    void* slot = oec_ringbuf_acquire(data_ring_, dropOldest ? 1 : 0, out_cap);
    if (!slot && dropOldest) ++dropped_;
    return (uint8_t*)slot;
}
void ShmemTransport::publishData(uint32_t) { oec_ringbuf_publish(data_ring_); }

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
