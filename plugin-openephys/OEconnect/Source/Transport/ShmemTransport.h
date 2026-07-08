#pragma once
#include "ITransport.h"
#include <memory>
#include <string>

extern "C" {
#include "oeconnect/shm.h"
#include "oeconnect/ringbuf.h"
}

namespace oec::plugin {

class ShmemTransport final : public ITransport {
public:
    ShmemTransport();
    ~ShmemTransport() override;

    bool start(const std::string& shm_name) override;
    void stop() override;

    uint8_t* acquireDataSlot(uint32_t* out_cap, bool dropOldest) override;
    void publishData(uint32_t bytes_written) override;

    uint8_t* acquireAckSlot(uint32_t* out_cap) override;
    void publishAck(uint32_t bytes_written) override;

    const uint8_t* peekCmd(uint32_t* out_size) override;
    void consumeCmd() override;

    void noteDropped() override { ++dropped_; lost_data_pending_ = true; }
    uint16_t consumePendingFlags() override {
        if (!lost_data_pending_) return 0;
        lost_data_pending_ = false;
        return OEC_FLAG_LOST_DATA;
    }
    uint64_t totalDropped() const override { return dropped_; }
    std::string name() const override { return "SharedMem"; }

private:
    oec_shm_t*           shm_ = nullptr;
    void*                mapped_ = nullptr;
    size_t               mapped_size_ = 0;
    oec_region_header_t* region_header_ = nullptr;
    oec_ringbuf_t* data_ring_ = nullptr;
    oec_ringbuf_t* cmd_ring_  = nullptr;
    oec_ringbuf_t* ack_ring_  = nullptr;
    uint64_t       dropped_ = 0;
    bool           lost_data_pending_ = false;
    uint64_t       last_evictions_ = 0;   /* to detect ring-full evictions */
    std::string    name_;
};

}  // namespace oec::plugin
