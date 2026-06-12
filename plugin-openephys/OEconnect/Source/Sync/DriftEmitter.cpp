#include "DriftEmitter.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

namespace {
#if defined(_WIN32)
uint64_t qpc_now() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t)c.QuadPart;
}
#else
uint64_t qpc_now() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
#endif
}  // namespace

void DriftEmitter::start(double fpga_sample_rate_hz) {
    stop();
    sample_rate_ = fpga_sample_rate_hz;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&DriftEmitter::loop, this);
}

void DriftEmitter::stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        if (thread_.joinable()) thread_.join();
    }
}

void DriftEmitter::loop() {
    using namespace std::chrono;
    while (running_.load(std::memory_order_acquire)) {
        AckEntry e{};
        e.cmd_id = OEC_STREAM_SYNC;        /* repurposed: stream id, not cmd id */
        e.cookie = 0;
        e.status = 0;
        e.sample_index = sample_index_.load(std::memory_order_relaxed);
        e.host_qpc_ticks = qpc_now();
        outbox_.tryPush(e);
        std::this_thread::sleep_for(seconds(1));
    }
}

}  // namespace oec::plugin
