#pragma once
#include "ITransport.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace oec::plugin {

/**
 * ZMQ-backed transport.
 *
 *   data path:  audio thread fills an internal SPSC ringbuf ->
 *               shipper thread drains and PUBs on `tcp_pub_endpoint`.
 *   cmd  path:  shipper thread REPs on `tcp_rep_endpoint`,
 *               pushes CMD bytes into internal cmd queue (audio thread reads).
 *   ack  path:  audio thread pushes ACK frames into internal ack queue ->
 *               shipper thread matches cookies and sends REP replies.
 */
class ZmqTransport final : public ITransport {
public:
    ZmqTransport();
    ~ZmqTransport() override;

    /** endpoint format: "tcp://*:5557|tcp://*:5558" (pub|rep). */
    bool start(const std::string& endpoint_pair) override;
    void stop() override;

    /** Sizes the internal ring so one slot holds one frame. Call before start(). */
    void configure(uint32_t slot_size);

    uint8_t* acquireDataSlot(uint32_t* out_cap, bool dropOldest) override;
    void publishData(uint32_t bytes_written) override;
    bool publishLargeFrame(const oec_frame_header_t& header,
                           const void* payload, size_t payload_len) override;

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
    std::string name() const override { return "Zmq"; }

private:
    void shipperLoop();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::thread shipper_;
    uint64_t dropped_ = 0;
    bool     lost_data_pending_ = false;
    uint32_t slot_size_ = 65536u;   /* one slot == one PUB message */
};

}  // namespace oec::plugin
