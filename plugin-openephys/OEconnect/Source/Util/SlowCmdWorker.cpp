#include "Util/SlowCmdWorker.h"

extern "C" {
#include "oeconnect/frame.h"
}

namespace oec::plugin {

void SlowCmdWorker::start() {
    stop();
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&SlowCmdWorker::loop, this);
}

void SlowCmdWorker::stop() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
}

bool SlowCmdWorker::tryEnqueue(SlowCmdRequest req) {
    /* Claim the single in-flight slot. CAS so two audio-thread calls (or a retry
     * racing the worker's completion) cannot both win. */
    int expected = 0;
    if (!in_flight_.compare_exchange_strong(expected, 1,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
        return false;   // caller answers ACK(BUSY)
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(req));
    }
    cv_.notify_one();
    return true;
}

void SlowCmdWorker::loop() {
    while (running_.load(std::memory_order_acquire)) {
        SlowCmdRequest req;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return !q_.empty() || !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire)) {
                /* Shutting down with a command still claimed: release the slot so
                 * a restarted worker is not permanently BUSY. */
                in_flight_.store(0, std::memory_order_release);
                return;
            }
            req = std::move(q_.front());
            q_.pop_front();
        }
        uint16_t status = dispatcher_ ? dispatcher_(req) : OEC_ACK_NOT_SUPPORTED;
        AckEntry e{};
        e.cookie = req.cookie;
        e.status = status;
        outbox_.tryPush(e);

        /* Free the slot only now: the command is done, not merely dequeued. */
        in_flight_.store(0, std::memory_order_release);
    }
    in_flight_.store(0, std::memory_order_release);
}

}  // namespace oec::plugin
