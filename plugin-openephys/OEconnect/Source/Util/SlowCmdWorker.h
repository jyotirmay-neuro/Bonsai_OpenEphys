#pragma once
#include "Util/AckOutbox.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace oec::plugin {

struct SlowCmdRequest {
    uint16_t cmd_id;
    uint32_t cookie;
    std::string arg1;   /* e.g. directory */
    std::string arg2;   /* e.g. prefix    */
};

class SlowCmdWorker {
public:
    using Dispatcher = std::function<uint16_t(const SlowCmdRequest&)>;

    SlowCmdWorker(AckOutbox& outbox, Dispatcher dispatcher)
        : outbox_(outbox), dispatcher_(std::move(dispatcher)) {}
    ~SlowCmdWorker() { stop(); }

    void start();
    void stop();

    /**
     * Accept a slow command only if none is already queued or executing
     * (spec §5.5: one command in flight at a time; the loser gets ACK(BUSY)).
     * Returns false when busy, so the caller can answer BUSY instead of silently
     * stacking recordings behind each other.
     *
     * Called from the audio thread: takes the queue mutex only to push, and the
     * queue is sparse (start/stop record, start/stop acq).
     */
    bool tryEnqueue(SlowCmdRequest req);

    /** True while a command is queued or executing. */
    bool busy() const { return in_flight_.load(std::memory_order_acquire) != 0; }

private:
    void loop();

    AckOutbox&            outbox_;
    Dispatcher            dispatcher_;
    std::atomic<bool>     running_{false};
    /* Queued + executing. Cleared only after the dispatcher returns, so a second
     * command issued while the first is still writing to disk gets BUSY. */
    std::atomic<int>      in_flight_{0};
    std::thread           thread_;
    std::mutex            mu_;
    std::condition_variable cv_;
    std::deque<SlowCmdRequest> q_;
};

}  // namespace oec::plugin
