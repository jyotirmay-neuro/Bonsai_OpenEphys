#pragma once
#include "Util/AckOutbox.h"
#include <atomic>
#include <chrono>
#include <thread>

namespace oec::plugin {

/**
 * Once per second, pushes a SYNC AckEntry into the AckOutbox so the audio
 * thread can lift it into the ack_ring on the next process() tick.
 */
class DriftEmitter {
public:
    explicit DriftEmitter(AckOutbox& outbox, std::atomic<uint64_t>& sample_index)
        : outbox_(outbox), sample_index_(sample_index) {}
    ~DriftEmitter() { stop(); }

    void start(double fpga_sample_rate_hz);
    void stop();

private:
    void loop();

    AckOutbox&           outbox_;
    std::atomic<uint64_t>& sample_index_;
    std::atomic<bool>    running_{false};
    std::thread          thread_;
    double               sample_rate_ = 30000.0;
};

}  // namespace oec::plugin
