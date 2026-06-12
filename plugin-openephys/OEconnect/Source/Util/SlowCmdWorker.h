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
    void enqueue(SlowCmdRequest req);

private:
    void loop();

    AckOutbox&            outbox_;
    Dispatcher            dispatcher_;
    std::atomic<bool>     running_{false};
    std::thread           thread_;
    std::mutex            mu_;
    std::condition_variable cv_;
    std::deque<SlowCmdRequest> q_;
};

}  // namespace oec::plugin
