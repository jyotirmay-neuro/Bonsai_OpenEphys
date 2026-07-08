#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "Util/AckOutbox.h"

using oec::plugin::AckOutbox;
using oec::plugin::AckEntry;

TEST(AckOutbox, PushPopSingleThread) {
    AckOutbox box(8);
    AckEntry e{};
    EXPECT_FALSE(box.tryPop(e));

    AckEntry in{};
    in.cookie = 42;
    in.status = 0;
    EXPECT_TRUE(box.tryPush(in));
    EXPECT_TRUE(box.tryPop(e));
    EXPECT_EQ(e.cookie, 42u);
    EXPECT_FALSE(box.tryPop(e));
}

TEST(AckOutbox, FullReturnsFalse) {
    AckOutbox box(4);
    AckEntry in{};
    for (int i = 0; i < 4; ++i) {
        in.cookie = (uint32_t)i;
        EXPECT_TRUE(box.tryPush(in));
    }
    in.cookie = 999;
    EXPECT_FALSE(box.tryPush(in));
}

TEST(AckOutbox, MultiProducerSingleConsumer) {
    AckOutbox box(4096);
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 10'000;
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                AckEntry e{};
                e.cookie = (uint32_t)(p * kPerProducer + i);
                while (!box.tryPush(e)) std::this_thread::yield();
            }
        });
    }

    std::vector<uint32_t> seen;
    seen.reserve(kProducers * kPerProducer);
    while ((int)seen.size() < kProducers * kPerProducer) {
        AckEntry e{};
        if (box.tryPop(e)) {
            seen.push_back(e.cookie);
        } else {
            std::this_thread::yield();
        }
    }
    for (auto &t : producers) t.join();

    std::vector<int> counts(kProducers * kPerProducer, 0);
    for (auto v : seen) counts[v]++;
    for (auto c : counts) EXPECT_EQ(c, 1);
}

/* ---- SlowCmdWorker: spec §5.5, one command in flight at a time ---- */

#include "Util/SlowCmdWorker.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

using oec::plugin::SlowCmdWorker;
using oec::plugin::SlowCmdRequest;

TEST(SlowCmdWorker, SecondEnqueueIsRefusedWhileFirstIsStillExecuting) {
    AckOutbox outbox(64);

    std::mutex m;
    std::condition_variable cv;
    bool release_dispatcher = false;
    std::atomic<int> dispatched{0};

    SlowCmdWorker worker(outbox, [&](const SlowCmdRequest&) -> uint16_t {
        dispatched.fetch_add(1);
        /* Hold the command "in flight" until the test lets go. */
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return release_dispatcher; });
        return 2 /* OEC_ACK_COMPLETED */;
    });
    worker.start();

    SlowCmdRequest a; a.cmd_id = 1; a.cookie = 0xA;
    EXPECT_TRUE(worker.tryEnqueue(a)) << "first command claims the slot";

    /* Wait until the worker has actually picked it up and is inside the dispatcher. */
    for (int i = 0; i < 200 && dispatched.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_EQ(dispatched.load(), 1);

    /* Dequeued but NOT finished: the slot must still be held. */
    SlowCmdRequest b; b.cmd_id = 2; b.cookie = 0xB;
    EXPECT_FALSE(worker.tryEnqueue(b)) << "slot is held until the dispatcher returns";
    EXPECT_TRUE(worker.busy());

    { std::lock_guard<std::mutex> lk(m); release_dispatcher = true; }
    cv.notify_all();

    /* Once complete, the slot frees up again. */
    for (int i = 0; i < 200 && worker.busy(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_FALSE(worker.busy());
    EXPECT_TRUE(worker.tryEnqueue(b)) << "slot reusable after completion";

    { std::lock_guard<std::mutex> lk(m); release_dispatcher = true; }
    cv.notify_all();
    worker.stop();
}

TEST(SlowCmdWorker, StopReleasesTheInFlightSlot) {
    AckOutbox outbox(64);
    SlowCmdWorker worker(outbox, [](const SlowCmdRequest&) -> uint16_t { return 2; });
    worker.start();
    worker.stop();
    EXPECT_FALSE(worker.busy()) << "a stopped worker must not stay permanently BUSY";
}
