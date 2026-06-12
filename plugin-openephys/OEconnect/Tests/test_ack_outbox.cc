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
