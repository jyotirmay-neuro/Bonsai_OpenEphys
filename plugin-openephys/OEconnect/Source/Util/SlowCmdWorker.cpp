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

void SlowCmdWorker::enqueue(SlowCmdRequest req) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(req));
    }
    cv_.notify_one();
}

void SlowCmdWorker::loop() {
    while (running_.load(std::memory_order_acquire)) {
        SlowCmdRequest req;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] {
                return !q_.empty() || !running_.load(std::memory_order_acquire);
            });
            if (!running_.load(std::memory_order_acquire)) return;
            req = std::move(q_.front());
            q_.pop_front();
        }
        uint16_t status = dispatcher_ ? dispatcher_(req) : OEC_ACK_NOT_SUPPORTED;
        AckEntry e{};
        e.cookie = req.cookie;
        e.status = status;
        outbox_.tryPush(e);
    }
}

}  // namespace oec::plugin
